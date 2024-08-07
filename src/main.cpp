#include "voxel-engine/camera.hpp"
#include "voxel-engine/filesystem.hpp"
#include "voxel-engine/renderer.hpp"
#include "voxel-engine/shader_compiler.hpp"
#include "voxel-engine/timer.hpp"
#include "voxel-engine/voxel.hpp"
#include "voxel-engine/window.hpp"

#include "shaders/interop/render_resources.hlsli"

#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

int main()
{
    printf("%s\n", FileSystem::instance().executable_path().c_str());

    const Window window{};
    Renderer renderer(window.get_handle(), window.get_width(), window.get_height());

    // Setup imgui.
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGui::StyleColorsDark();

        D3D12_CPU_DESCRIPTOR_HANDLE cpu_descriptor_handle =
            renderer.m_cbv_srv_uav_descriptor_heap.current_cpu_descriptor_handle;

        D3D12_GPU_DESCRIPTOR_HANDLE gpu_descriptor_handle =
            renderer.m_cbv_srv_uav_descriptor_heap.current_gpu_descriptor_handle;

        renderer.m_cbv_srv_uav_descriptor_heap.offset_current_descriptor_handles();

        // Setup platform / renderer backend.
        ImGui_ImplWin32_Init(window.get_handle());
        ImGui_ImplDX12_Init(renderer.m_device.Get(), Renderer::NUMBER_OF_BACKBUFFERS, Renderer::BACKBUFFER_FORMAT,
                            renderer.m_cbv_srv_uav_descriptor_heap.descriptor_heap.Get(), cpu_descriptor_handle,
                            gpu_descriptor_handle);
    }

    ChunkManager chunk_manager{renderer};

    SceneConstantBuffer scene_buffer_data{};

    // Setup the AABB data for scene buffer.

    // AABB for chunk.
    static constexpr std::array<DirectX::XMFLOAT4, 8> aabb_vertices{
        DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f),
        DirectX::XMFLOAT4(0.0f, Chunk::CHUNK_LENGTH, 0.0f, 1.0f),
        DirectX::XMFLOAT4(Chunk::CHUNK_LENGTH, Chunk::CHUNK_LENGTH, 0.0f, 1.0f),
        DirectX::XMFLOAT4(Chunk::CHUNK_LENGTH, 0.0f, 0.0f, 1.0f),
        DirectX::XMFLOAT4(0.0f, 0.0f, Chunk::CHUNK_LENGTH, 1.0f),
        DirectX::XMFLOAT4(0.0f, Chunk::CHUNK_LENGTH, Chunk::CHUNK_LENGTH, 1.0f),
        DirectX::XMFLOAT4(Chunk::CHUNK_LENGTH, Chunk::CHUNK_LENGTH, Chunk::CHUNK_LENGTH, 1.0f),
        DirectX::XMFLOAT4(Chunk::CHUNK_LENGTH, 0.0f, Chunk::CHUNK_LENGTH, 1.0f),
    };

    for (int i = 0; i < 8; i++)
    {
        scene_buffer_data.aabb_vertices[i] = aabb_vertices[i];
    }

    auto scene_buffers = renderer.create_constant_buffer<Renderer::NUMBER_OF_BACKBUFFERS>(sizeof(SceneConstantBuffer),
                                                                                          L"Scene constant buffer");

    // Compile the vertex and pixel shader.
    Microsoft::WRL::ComPtr<IDxcBlob> vertex_shader_blob = ShaderCompiler::compile(
        FileSystem::instance().get_relative_path_wstr(L"shaders/voxel_shader.hlsl").c_str(), L"vs_main", L"vs_6_6");

    Microsoft::WRL::ComPtr<IDxcBlob> pixel_shader_blob = ShaderCompiler::compile(
        FileSystem::instance().get_relative_path_wstr(L"shaders/voxel_shader.hlsl").c_str(), L"ps_main", L"ps_6_6");

    // Setup depth buffer.
    Microsoft::WRL::ComPtr<ID3D12Resource> depth_buffer_resource{};
    const D3D12_RESOURCE_DESC depth_buffer_resource_desc = {
        .Dimension = D3D12_RESOURCE_DIMENSION::D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        .Width = window.get_width(),
        .Height = window.get_height(),
        .DepthOrArraySize = 1u,
        .MipLevels = 1u,
        .Format = DXGI_FORMAT_D32_FLOAT,
        .SampleDesc = {1u, 0u},
        .Layout = D3D12_TEXTURE_LAYOUT::D3D12_TEXTURE_LAYOUT_UNKNOWN,
        .Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
    };

    const D3D12_HEAP_PROPERTIES depth_buffer_heap_properties = {
        .Type = D3D12_HEAP_TYPE_DEFAULT,
        .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
        .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
        .CreationNodeMask = 0u,
        .VisibleNodeMask = 0u,
    };

    const D3D12_CLEAR_VALUE depth_buffer_optimized_clear_value = {
        .Format = DXGI_FORMAT_D32_FLOAT,
        .DepthStencil = {.Depth = 0.0f, .Stencil = 0u},
    };

    throw_if_failed(renderer.m_device->CreateCommittedResource(
        &depth_buffer_heap_properties, D3D12_HEAP_FLAG_NONE, &depth_buffer_resource_desc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &depth_buffer_optimized_clear_value, IID_PPV_ARGS(&depth_buffer_resource)));

    // Create DSV.
    D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle = renderer.m_dsv_descriptor_heap.current_cpu_descriptor_handle;
    {
        const D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {
            .Format = DXGI_FORMAT_D32_FLOAT,
            .ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D,
            .Flags = D3D12_DSV_FLAG_NONE,
            .Texture2D =
                {

                    .MipSlice = 0u,
                },
        };

        renderer.m_dsv_descriptor_heap.offset_current_descriptor_handles();

        renderer.m_device->CreateDepthStencilView(depth_buffer_resource.Get(), &dsv_desc, dsv_handle);
    }

    // Create the PSO.
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso{};
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC graphics_pso_desc = {
        .pRootSignature = renderer.m_bindless_root_signature.Get(),
        .VS =
            {
                .pShaderBytecode = vertex_shader_blob->GetBufferPointer(),
                .BytecodeLength = vertex_shader_blob->GetBufferSize(),
            },
        .PS =
            {
                .pShaderBytecode = pixel_shader_blob->GetBufferPointer(),
                .BytecodeLength = pixel_shader_blob->GetBufferSize(),
            },
        .BlendState =
            {
                .AlphaToCoverageEnable = FALSE,
                .IndependentBlendEnable = FALSE,
                .RenderTarget =
                    {
                        D3D12_RENDER_TARGET_BLEND_DESC{
                            .BlendEnable = FALSE,
                            .RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL,
                        },
                    },
            },
        .SampleMask = 0xffff'ffff,
        .RasterizerState =
            {
                .FillMode = D3D12_FILL_MODE_SOLID,
                .CullMode = D3D12_CULL_MODE_BACK,
                .FrontCounterClockwise = FALSE,
                .DepthClipEnable = TRUE,
            },
        .DepthStencilState =
            {
                .DepthEnable = TRUE,
                .DepthWriteMask = D3D12_DEPTH_WRITE_MASK::D3D12_DEPTH_WRITE_MASK_ALL,
                .DepthFunc = D3D12_COMPARISON_FUNC_GREATER,
                .StencilEnable = FALSE,
            },
        .InputLayout =
            {
                .NumElements = 0u,
            },
        .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
        .NumRenderTargets = 1u,
        .RTVFormats =
            {
                Renderer::BACKBUFFER_FORMAT,
            },
        .DSVFormat = DXGI_FORMAT_D32_FLOAT,
        .SampleDesc =
            {
                1u,
                0u,
            },
        .NodeMask = 0u,
    };
    throw_if_failed(renderer.m_device->CreateGraphicsPipelineState(&graphics_pso_desc, IID_PPV_ARGS(&pso)));

    // Setup the gpu culling compute shader.
    Microsoft::WRL::ComPtr<IDxcBlob> gpu_culling_compute_shader_blob = ShaderCompiler::compile(
        FileSystem::instance().get_relative_path_wstr(L"shaders/gpu_culling_shader.hlsl").c_str(), L"cs_main",
        L"cs_6_6");

    Microsoft::WRL::ComPtr<ID3D12PipelineState> gpu_culling_pso{};
    const D3D12_COMPUTE_PIPELINE_STATE_DESC gpu_culling_compute_pso_desc = {
        .pRootSignature = renderer.m_bindless_root_signature.Get(),
        .CS =
            {
                .pShaderBytecode = gpu_culling_compute_shader_blob->GetBufferPointer(),
                .BytecodeLength = gpu_culling_compute_shader_blob->GetBufferSize(),
            },
        .NodeMask = 0u,
        .Flags = D3D12_PIPELINE_STATE_FLAG_NONE,
    };
    throw_if_failed(
        renderer.m_device->CreateComputePipelineState(&gpu_culling_compute_pso_desc, IID_PPV_ARGS(&gpu_culling_pso)));

    // Indirect command struct : command signature must match this struct.
    // Each chunk will have its own IndirectCommand, with 3 arguments. The render resources struct root constants, index
    // buffer view and a draw call.
    struct IndirectCommand
    {
        VoxelRenderResources render_resources{};
        D3D12_INDEX_BUFFER_VIEW index_buffer_view{};
        D3D12_DRAW_INDEXED_ARGUMENTS draw_arguments{};
        float padding;
    };

    printf("Size of indirect command : %zd\n", sizeof(IndirectCommand));

    // Create the command signature, which tells the GPU how to interpret the data passed in the ExecuteIndirect call.
    const std::array<D3D12_INDIRECT_ARGUMENT_DESC, 3u> argument_descs = {
        D3D12_INDIRECT_ARGUMENT_DESC{
            .Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT,
            .Constant =
                {
                    .RootParameterIndex = 0u,
                    .DestOffsetIn32BitValues = 0u,
                    .Num32BitValuesToSet = sizeof(VoxelRenderResources) / sizeof(u32),
                },
        },
        D3D12_INDIRECT_ARGUMENT_DESC{
            .Type = D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW,
        },
        D3D12_INDIRECT_ARGUMENT_DESC{
            .Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED,
        },
    };

    Microsoft::WRL::ComPtr<ID3D12CommandSignature> command_signature{};
    const D3D12_COMMAND_SIGNATURE_DESC command_signature_desc = {
        .ByteStride = sizeof(IndirectCommand),
        .NumArgumentDescs = argument_descs.size(),
        .pArgumentDescs = argument_descs.data(),
        .NodeMask = 0u,
    };

    throw_if_failed(renderer.m_device->CreateCommandSignature(
        &command_signature_desc, renderer.m_bindless_root_signature.Get(), IID_PPV_ARGS(&command_signature)));

    // Command buffer that will be used to store the indirect command args.
    static constexpr size_t MAX_CHUNKS_TO_BE_DRAWN = 10'00'000;
    std::vector<IndirectCommand> indirect_command_vector{};

    CommandBuffer indirect_command_buffer =
        renderer.create_command_buffer(sizeof(IndirectCommand), MAX_CHUNKS_TO_BE_DRAWN, L"Indirect Command Buffer");

    // Create viewport and scissor.
    const D3D12_VIEWPORT viewport = {
        .TopLeftX = 0.0f,
        .TopLeftY = 0.0f,
        .Width = (float)window.get_width(),
        .Height = (float)window.get_height(),
        .MinDepth = 0.0f,
        .MaxDepth = 1.0f,
    };

    // The default config is used if we want to mask the entire viewport for drawing.
    const D3D12_RECT scissor_rect = {
        .left = 0u,
        .top = 0u,
        .right = LONG_MAX,
        .bottom = LONG_MAX,
    };

    // Execute and flush gpu so resources required for rendering (before the first frame) are ready.
    renderer.m_copy_queue.flush_queue();

    renderer.m_direct_queue.execute_command_list();
    renderer.m_direct_queue.flush_queue();

    // Precompute the offset to a chunk index X, using which we can load chunks within the CHUNK_RENDER_DISTANCE volume
    // around the player at any given moment.
    // For precomputation, X is assumed to be zero. These values will be added to the current chunk index.
    // NOTE : The current player chunk is loaded first, then the chunks 1 distance away, then 2 distance away, etc.
    // note(rtarun9) : Make this more efficient?
    std::vector<DirectX::XMINT3> chunk_render_distance_offsets = {};
    chunk_render_distance_offsets.push_back(DirectX::XMINT3{0, 0, 0});

    for (i32 z = -1 * ChunkManager::CHUNK_RENDER_DISTANCE; z <= (i32)ChunkManager::CHUNK_RENDER_DISTANCE; z++)
    {
        for (i32 y = -ChunkManager::CHUNK_RENDER_DISTANCE; y <= (i32)ChunkManager::CHUNK_RENDER_DISTANCE; y++)
        {
            for (i32 x = -ChunkManager::CHUNK_RENDER_DISTANCE; x <= (i32)ChunkManager::CHUNK_RENDER_DISTANCE; x++)
            {
                if ((z == -ChunkManager::CHUNK_RENDER_DISTANCE || z == ChunkManager::CHUNK_RENDER_DISTANCE) ||
                    (y == -ChunkManager::CHUNK_RENDER_DISTANCE || y == ChunkManager::CHUNK_RENDER_DISTANCE) ||
                    (x == -ChunkManager::CHUNK_RENDER_DISTANCE || x == ChunkManager::CHUNK_RENDER_DISTANCE))
                {
                    chunk_render_distance_offsets.emplace_back(DirectX::XMINT3{x, y, z});
                }
            }
        }
    }
    std::sort(chunk_render_distance_offsets.begin(), chunk_render_distance_offsets.end(),
              [](const DirectX::XMINT3 &a, const DirectX::XMINT3 &b) {
                  return a.x * a.x + a.y * a.y + a.z * a.z < b.x * b.x + b.y * b.y + b.z * b.z;
              });

    Camera camera{};
    const u64 chunk_grid_middle = Chunk::CHUNK_LENGTH * ChunkManager::NUMBER_OF_CHUNKS_PER_DIMENSION / 2u;
    camera.m_position = {chunk_grid_middle, chunk_grid_middle, chunk_grid_middle, 1.0f};

    std::queue<u64> chunks_to_unload{};

    Timer timer{};
    float delta_time = 0.0f;

    bool setup_chunks{false};

    u64 frame_count = 0;

    bool quit{false};
    while (!quit)
    {
        static float near_plane = 1.0f;
        static float far_plane = 1000000.0f;

        // Get the player's current chunk index.

        const DirectX::XMUINT3 current_chunk_3d_index = {
            (u32)(floor((camera.m_position.x) / Chunk::CHUNK_LENGTH)),
            (u32)(floor((camera.m_position.y) / Chunk::CHUNK_LENGTH)),
            (u32)(floor((camera.m_position.z) / Chunk::CHUNK_LENGTH)),
        };

        const u64 current_chunk_index =
            convert_to_1d(current_chunk_3d_index, ChunkManager::NUMBER_OF_CHUNKS_PER_DIMENSION);

        if (setup_chunks)
        {
            // Load chunks around the player (ChunkManager::CHUNK_RENDER_DISTANCE) determines how many of these
            // chunks to load.
            for (const auto &offset : chunk_render_distance_offsets)
            {
                const DirectX::XMUINT3 chunk_3d_index = {
                    current_chunk_3d_index.x + offset.x,
                    current_chunk_3d_index.y + offset.y,
                    current_chunk_3d_index.z + offset.z,
                };

                chunk_manager.add_chunk_to_setup_stack(
                    convert_to_1d(chunk_3d_index, ChunkManager::NUMBER_OF_CHUNKS_PER_DIMENSION));
            }
        }

        chunk_manager.create_chunks_from_setup_stack(renderer);

        timer.start();

        MSG message = {};
        if (PeekMessageA(&message, NULL, 0u, 0u, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }

        if (message.message == WM_QUIT)
        {
            quit = true;
        }

        chunk_manager.transfer_chunks_from_setup_to_loaded_state(renderer.m_copy_queue.m_fence->GetCompletedValue());

        const float window_aspect_ratio = static_cast<float>(window.get_width()) / window.get_height();

        // Article followed for reverse Z:
        //  https://iolite-engine.com/blog_posts/reverse_z_cheatsheet

        // https://github.com/microsoft/DirectXMath/issues/158 link that shows the projection matrix for infinite far
        // plane.
        // Note : This code is taken from the directxmath source code for perspective projection fov lh, but modified
        // for infinite far plane.

        float sin_fov{};
        float cos_fov{};
        DirectX::XMScalarSinCos(&sin_fov, &cos_fov, 0.5f * DirectX::XMConvertToRadians(45.0f));

        float height = cos_fov / sin_fov;
        float width = height / window_aspect_ratio;

        const DirectX::XMMATRIX projection_matrix = DirectX::XMMatrixSet(
            width, 0.0f, 0.0f, 0.0f, 0.0f, height, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, near_plane, 0.0f);

        scene_buffer_data.view_matrix = camera.update_and_get_view_matrix(delta_time);
        scene_buffer_data.projection_matrix = projection_matrix;
        scene_buffer_data.camera_position = camera.m_position;

        ConstantBuffer &scene_buffer = scene_buffers[renderer.m_swapchain_backbuffer_index];
        scene_buffer.update(&scene_buffer_data);

        const auto &swapchain_index = renderer.m_swapchain_backbuffer_index;

        // Reset command allocator and command list.
        renderer.m_direct_queue.reset(swapchain_index);

        const auto &command_list = renderer.m_direct_queue.m_command_list;

        const auto &rtv_handle = renderer.m_swapchain_backbuffer_cpu_descriptor_handles[swapchain_index];
        const Microsoft::WRL::ComPtr<ID3D12Resource> swapchain_resource =
            renderer.m_swapchain_backbuffer_resources[swapchain_index];

        // Transition the backbuffer from presentation mode to render target mode.
        const D3D12_RESOURCE_BARRIER presentation_to_render_target_barrier = {
            .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
            .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
            .Transition =
                {
                    .pResource = swapchain_resource.Get(),
                    .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                    .StateBefore = D3D12_RESOURCE_STATE_PRESENT,
                    .StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET,
                },
        };

        command_list->ResourceBarrier(1u, &presentation_to_render_target_barrier);

        // Now, clear the RTV and DSV.
        const float clear_color[4] = {0.1f, 0.1f, 0.1f, 1.0f};
        command_list->ClearRenderTargetView(rtv_handle, clear_color, 0u, nullptr);
        command_list->ClearDepthStencilView(dsv_handle, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0u, 0u, nullptr);

        // Set viewport.
        command_list->RSSetViewports(1u, &viewport);
        command_list->RSSetScissorRects(1u, &scissor_rect);

        // Evict the chunks that are out of range of render distance.
        // Because evicting chunks seems to have such a high overhead (more specifically freeing allocated id3d12
        // resources) each frame only a certain number of chunks are unloaded.
        for (const auto &[i, chunk] : chunk_manager.m_loaded_chunks)
        {
            const DirectX::XMUINT3 chunk_index_3d = convert_to_3d(i, ChunkManager::NUMBER_OF_CHUNKS_PER_DIMENSION);
            if (std::abs((i32)chunk_index_3d.x - (i32)current_chunk_3d_index.x) >
                    ChunkManager::CHUNK_RENDER_DISTANCE * 8 ||
                std::abs((i32)chunk_index_3d.y - (i32)current_chunk_3d_index.y) >
                    ChunkManager::CHUNK_RENDER_DISTANCE * 8 ||
                std::abs((i32)chunk_index_3d.z - (i32)current_chunk_3d_index.z) >
                    ChunkManager::CHUNK_RENDER_DISTANCE * 8)
            {
                chunks_to_unload.push(i);
            }
        }

        /*
        size_t unloaded_chunks = 0;
        while (false && !chunks_to_unload.empty() && unloaded_chunks < ChunkManager::CHUNKS_TO_UNLOAD_PER_FRAME)
        {
            const auto chunk_to_unload = chunks_to_unload.front();
            chunks_to_unload.pop();

            chunk_manager.m_loaded_chunks.erase(chunk_to_unload);

            chunk_manager.m_chunk_index_buffers.erase(chunk_to_unload);
            chunk_manager.m_chunk_index_buffers[chunk_to_unload].resource.Reset();

            chunk_manager.m_chunk_color_buffers.erase(chunk_to_unload);
            chunk_manager.m_chunk_color_buffers[chunk_to_unload].resource.Reset();

            chunk_manager.m_chunk_constant_buffers[chunk_to_unload].resource.Reset();
            chunk_manager.m_chunk_constant_buffers.erase(chunk_to_unload);

            ++unloaded_chunks;
        }
        */

        // Setup indirect command vector.
        indirect_command_vector.clear();
        indirect_command_vector.reserve(chunk_manager.m_loaded_chunks.size());
        for (const auto &[i, chunk] : chunk_manager.m_loaded_chunks)
        {
            const VoxelRenderResources render_resources = {
                .scene_constant_buffer_index = static_cast<u32>(scene_buffer.cbv_index),
                .chunk_constant_buffer_index = static_cast<u32>(chunk_manager.m_chunk_constant_buffers[i].cbv_index),
            };

            indirect_command_vector.emplace_back(IndirectCommand{
                .render_resources = render_resources,
                .index_buffer_view = chunk_manager.m_chunk_index_buffers[i].index_buffer_view,
                .draw_arguments =
                    D3D12_DRAW_INDEXED_ARGUMENTS{
                        .IndexCountPerInstance = (u32)chunk_manager.m_chunk_index_buffers[i].indices_count,
                        .InstanceCount = 1u,
                        .StartIndexLocation = 0u,
                        .BaseVertexLocation = 0u,
                        .StartInstanceLocation = 0u,
                    },
            });
        }

        ID3D12DescriptorHeap *const *shader_visible_descriptor_heaps = {
            renderer.m_cbv_srv_uav_descriptor_heap.descriptor_heap.GetAddressOf(),
        };

        command_list->SetDescriptorHeaps(1u, shader_visible_descriptor_heaps);

        // Prepare rendering commands.
        command_list->OMSetRenderTargets(1u, &rtv_handle, FALSE, &dsv_handle);

        // Run the culling compute shader, followed by voxel rendering shader.
        if (!indirect_command_vector.empty())
        {
            const D3D12_RESOURCE_BARRIER indirect_argument_to_copy_dest_state = {
                .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                .Flags = D3D12_RESOURCE_BARRIER_FLAGS::D3D12_RESOURCE_BARRIER_FLAG_NONE,
                .Transition =
                    D3D12_RESOURCE_TRANSITION_BARRIER{
                        .pResource = indirect_command_buffer.default_resource.Get(),
                        .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                        .StateBefore = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
                        .StateAfter = D3D12_RESOURCE_STATE_COPY_DEST,
                    },
            };
            command_list->ResourceBarrier(1u, &indirect_argument_to_copy_dest_state);

            memcpy(indirect_command_buffer.upload_resource_mapped_ptr, indirect_command_vector.data(),
                   indirect_command_vector.size() * sizeof(IndirectCommand));

            GPUCullRenderResources gpu_cull_render_resources = {
                .number_of_chunks = static_cast<u32>(indirect_command_vector.size()),
                .indirect_command_srv_index = static_cast<u32>(indirect_command_buffer.upload_resource_srv_index),
                .output_command_uav_index = static_cast<u32>(indirect_command_buffer.default_resource_uav_index),
                .scene_constant_buffer_index = static_cast<u32>(scene_buffer.cbv_index),
            };

            command_list->SetDescriptorHeaps(1u, shader_visible_descriptor_heaps);
            command_list->SetComputeRootSignature(renderer.m_bindless_root_signature.Get());
            command_list->SetPipelineState(gpu_culling_pso.Get());

            command_list->SetComputeRoot32BitConstants(0u, 64u, &gpu_cull_render_resources, 0u);

            // Clear the counter associated with UAV.

            command_list->CopyBufferRegion(indirect_command_buffer.default_resource.Get(),
                                           indirect_command_buffer.counter_offset,
                                           indirect_command_buffer.zeroed_counter_buffer_resource.Get(), 0u, 4u);

            const D3D12_RESOURCE_BARRIER copy_dest_to_unordered_access_state = {
                .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                .Flags = D3D12_RESOURCE_BARRIER_FLAGS::D3D12_RESOURCE_BARRIER_FLAG_NONE,
                .Transition =
                    D3D12_RESOURCE_TRANSITION_BARRIER{
                        .pResource = indirect_command_buffer.default_resource.Get(),
                        .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                        .StateBefore = D3D12_RESOURCE_STATE_COPY_DEST,
                        .StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    },

            };

            command_list->ResourceBarrier(1u, &copy_dest_to_unordered_access_state);

            command_list->Dispatch((indirect_command_vector.size() + 31) / 32u, 1u, 1u);

            const D3D12_RESOURCE_BARRIER unordered_access_to_indirect_argument_state = {
                .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                .Flags = D3D12_RESOURCE_BARRIER_FLAGS::D3D12_RESOURCE_BARRIER_FLAG_NONE,
                .Transition =
                    D3D12_RESOURCE_TRANSITION_BARRIER{
                        .pResource = indirect_command_buffer.default_resource.Get(),
                        .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                        .StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        .StateAfter = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
                    },

            };

            command_list->ResourceBarrier(1u, &unordered_access_to_indirect_argument_state);

            command_list->SetDescriptorHeaps(1u, shader_visible_descriptor_heaps);
            command_list->SetGraphicsRootSignature(renderer.m_bindless_root_signature.Get());
            command_list->SetPipelineState(pso.Get());

            command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            command_list->ExecuteIndirect(
                command_signature.Get(), MAX_CHUNKS_TO_BE_DRAWN, indirect_command_buffer.default_resource.Get(), 0u,
                indirect_command_buffer.default_resource.Get(), indirect_command_buffer.counter_offset);
        }

        // Render UI.
        // Start the Dear ImGui frame

        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Debug Controller");
        ImGui::SliderFloat("movement_speed", &camera.m_movement_speed, 0.0f, 5000.0f);
        ImGui::SliderFloat("rotation_speed", &camera.m_rotation_speed, 0.0f, 10.0f);
        ImGui::SliderFloat("friction", &camera.m_friction, 0.0f, 1.0f);
        ImGui::SliderFloat("near plane", &near_plane, 0.1f, 1.0f);
        ImGui::SliderFloat("Far plane", &far_plane, 10.0f, 10000000.0f);
        ImGui::Checkbox("Start loading chunks", &setup_chunks);
        ImGui::Text("Delta Time: %f", delta_time);
        ImGui::Text("Camera Position : %f %f %f", camera.m_position.x, camera.m_position.y, camera.m_position.z);
        ImGui::Text("Pitch and Yaw: %f %f", camera.m_pitch, camera.m_yaw);
        ImGui::Text("Current Index: %zu", current_chunk_index);
        ImGui::Text("Current 3D Index: %zu, %zu, %zu", current_chunk_3d_index.x, current_chunk_3d_index.y,
                    current_chunk_3d_index.z);
        ImGui::Text("Number of loaded chunks: %zu", chunk_manager.m_loaded_chunks.size());
        ImGui::Text("Number of rendered chunks: %zu", indirect_command_vector.size());
        ImGui::Text("Number of copy alloc / list pairs : %zu",
                    renderer.m_copy_queue.m_command_allocator_list_queue.size());
        ImGui::Text("Voxel edge length : %zu", Voxel::EDGE_LENGTH);
        ImGui::Text("Number of threads in pool : %zu", chunk_manager.m_thread_pool.get_thread_count());
        ImGui::Text("Number of queued threads in pool : %zu", chunk_manager.m_thread_pool.get_tasks_queued());

        ImGui::ShowMetricsWindow();
        ImGui::End();

        command_list->SetDescriptorHeaps(1u, shader_visible_descriptor_heaps);
        ImGui::Render();
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), command_list.Get());

        // Now, transition back to presentation mode.
        const D3D12_RESOURCE_BARRIER render_target_to_presentation_barrier = {
            .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
            .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
            .Transition =
                {
                    .pResource = swapchain_resource.Get(),
                    .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                    .StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET,
                    .StateAfter = D3D12_RESOURCE_STATE_PRESENT,
                },
        };

        command_list->ResourceBarrier(1u, &render_target_to_presentation_barrier);

        // Submit command list to queue for execution.
        renderer.m_direct_queue.execute_command_list();

        // Now, present the rendertarget and signal command queue.
        throw_if_failed(renderer.m_swapchain->Present(1u, 0u));
        renderer.m_direct_queue.signal_fence(renderer.m_swapchain_backbuffer_index);

        renderer.m_swapchain_backbuffer_index = static_cast<u8>(renderer.m_swapchain->GetCurrentBackBufferIndex());

        // Wait for the previous frame (that is presenting to
        // swpachain_backbuffer_index) to complete execution.
        renderer.m_direct_queue.wait_for_fence_value_at_index(renderer.m_swapchain_backbuffer_index);

        ++frame_count;

        timer.stop();
        delta_time = timer.get_delta_time();
    }

    // Cleanup
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    renderer.m_direct_queue.flush_queue();
    renderer.m_copy_queue.flush_queue();

    return 0;
}