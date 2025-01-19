#include "voxel-engine/camera.hpp"
#include "voxel-engine/filesystem.hpp"
#include "voxel-engine/rhi/common.hpp"
#include "voxel-engine/rhi/renderer.hpp"
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
    printf("Executable Path :: %s\n", file_system_t::instance().executable_path().c_str());

    const window_t window{};
    rhi::renderer_t renderer(window.get_handle(), window.get_width(), window.get_height());

    // Setup imgui.
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGui::StyleColorsDark();

        rhi::descriptor_handle_t cbv_srv_uav_descriptor_handle =
            renderer.m_cbv_srv_uav_descriptor_heap.get_then_offset_current_descriptor_handle();

        // Setup platform / renderer backend.
        ImGui_ImplWin32_Init(window.get_handle());
        ImGui_ImplDX12_Init(renderer.m_device.Get(), rhi::NUMBER_OF_BACKBUFFERS, rhi::BACKBUFFER_FORMAT,
                            renderer.m_cbv_srv_uav_descriptor_heap.m_descriptor_heap.Get(),
                            cbv_srv_uav_descriptor_handle.m_cpu_descriptor_handle,
                            cbv_srv_uav_descriptor_handle.m_gpu_descriptor_handle);
    }

    voxel_chunk_manager_t chunk_manager{renderer};

    // Setup the AABB data for scene buffer.
    auto scene_buffers = renderer.create_constant_buffers<interop::scene_constant_buffer_t, rhi::NUMBER_OF_BACKBUFFERS>(
        L"Scene constant buffer");

    // AABB for chunk.
    static constexpr std::array<DirectX::XMFLOAT4, 8> aabb_vertices{
        DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f),
        DirectX::XMFLOAT4(0.0f, voxel_chunk_t::CHUNK_LENGTH, 0.0f, 1.0f),
        DirectX::XMFLOAT4(voxel_chunk_t::CHUNK_LENGTH, voxel_chunk_t::CHUNK_LENGTH, 0.0f, 1.0f),
        DirectX::XMFLOAT4(voxel_chunk_t::CHUNK_LENGTH, 0.0f, 0.0f, 1.0f),
        DirectX::XMFLOAT4(0.0f, 0.0f, voxel_chunk_t::CHUNK_LENGTH, 1.0f),
        DirectX::XMFLOAT4(0.0f, voxel_chunk_t::CHUNK_LENGTH, voxel_chunk_t::CHUNK_LENGTH, 1.0f),
        DirectX::XMFLOAT4(voxel_chunk_t::CHUNK_LENGTH, voxel_chunk_t::CHUNK_LENGTH, voxel_chunk_t::CHUNK_LENGTH, 1.0f),
        DirectX::XMFLOAT4(voxel_chunk_t::CHUNK_LENGTH, 0.0f, voxel_chunk_t::CHUNK_LENGTH, 1.0f),
    };

    for (int i = 0; i < 8; i++)
    {
        for (auto &scene_buffer : scene_buffers)
        {
            scene_buffer.m_data.aabb_vertices[i] = aabb_vertices[i];
        }
    }

    // Compile the vertex and pixel shader.
    ComPtr<IDxcBlob> vertex_shader_blob = shader_compiler::compile(
        file_system_t::instance().get_relative_path_wstr(L"shaders/voxel_shader.hlsl").c_str(), L"vs_main", L"vs_6_6");

    ComPtr<IDxcBlob> pixel_shader_blob = shader_compiler::compile(
        file_system_t::instance().get_relative_path_wstr(L"shaders/voxel_shader.hlsl").c_str(), L"ps_main", L"ps_6_6");

    // Setup depth buffer.
    ComPtr<ID3D12Resource> depth_buffer_resource{};
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
    name_d3d12_object(depth_buffer_resource.Get(), L"Depth buffer resource");

    // Create DSV.
    D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle =
        renderer.m_dsv_descriptor_heap.get_then_offset_current_descriptor_handle().m_cpu_descriptor_handle;
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

        renderer.m_device->CreateDepthStencilView(depth_buffer_resource.Get(), &dsv_desc, dsv_handle);
    }

    // Create the PSO.
    ComPtr<ID3D12PipelineState> pso{};
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
                rhi::BACKBUFFER_FORMAT,
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
    name_d3d12_object(pso.Get(), L"Voxel PSO");

    // Setup the gpu culling compute shader.
    ComPtr<IDxcBlob> gpu_culling_compute_shader_blob = shader_compiler::compile(
        file_system_t::instance().get_relative_path_wstr(L"shaders/gpu_culling_shader.hlsl").c_str(), L"cs_main",
        L"cs_6_6");

    ComPtr<ID3D12PipelineState> gpu_culling_pso{};
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
    name_d3d12_object(gpu_culling_pso.Get(), L"Gpu culling PSO");

    // Indirect command struct : command signature must match this struct.
    // Each chunk will have its own IndirectCommand, with 3 arguments. The render resources struct root constants, index
    // buffer view and a draw call.

#pragma pack(push, 4)
    struct indirect_command_t
    {
        interop::voxel_render_resources_t render_resources{};
        D3D12_INDEX_BUFFER_VIEW index_buffer_view{};
        D3D12_DRAW_INDEXED_ARGUMENTS draw_arguments{};
    };
#pragma pack(pop)

    // NOTE: gpu_indirect_command_t and indirect_command_t MUST match.
    static_assert(sizeof(interop::gpu_indirect_command_t) == sizeof(indirect_command_t));

    printf("Size of indirect command : %d\n", (i32)sizeof(indirect_command_t));
    printf("Size of gpu indirect command : %d\n", (i32)sizeof(interop::gpu_indirect_command_t));
    printf("Size of voxel render resources: %d\n", (i32)sizeof(interop::voxel_render_resources_t));

    // Create the command signature, which tells the GPU how to interpret the data passed in the ExecuteIndirect call.
    const std::array<D3D12_INDIRECT_ARGUMENT_DESC, 3u> argument_descs = {
        D3D12_INDIRECT_ARGUMENT_DESC{
            .Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT,
            .Constant =
                {
                    .RootParameterIndex = 0u,
                    .DestOffsetIn32BitValues = 0u,
                    .Num32BitValuesToSet = sizeof(interop::voxel_render_resources_t) / sizeof(u32),
                },
        },
        D3D12_INDIRECT_ARGUMENT_DESC{
            .Type = D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW,
        },
        D3D12_INDIRECT_ARGUMENT_DESC{
            .Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED,
        },
    };

    ComPtr<ID3D12CommandSignature> command_signature{};
    const D3D12_COMMAND_SIGNATURE_DESC command_signature_desc = {
        .ByteStride = sizeof(indirect_command_t),
        .NumArgumentDescs = argument_descs.size(),
        .pArgumentDescs = argument_descs.data(),
        .NodeMask = 0u,
    };

    throw_if_failed(renderer.m_device->CreateCommandSignature(
        &command_signature_desc, renderer.m_bindless_root_signature.Get(), IID_PPV_ARGS(&command_signature)));
    name_d3d12_object(command_signature.Get(), L"Command signature");

    // Command buffer that will be used to store the indirect command args.
    static constexpr size_t MAX_CHUNKS_TO_BE_DRAWN = 10'00'000;
    std::vector<indirect_command_t> indirect_command_vector{};
    indirect_command_vector.reserve(MAX_CHUNKS_TO_BE_DRAWN);

    rhi::command_buffer_t indirect_command_buffer =
        renderer.create_command_buffer(sizeof(indirect_command_t), MAX_CHUNKS_TO_BE_DRAWN, L"Indirect Command Buffer");

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
    std::vector<DirectX::XMINT3> chunk_render_distance_offsets = {};
    chunk_render_distance_offsets.push_back(DirectX::XMINT3{0, 0, 0});

    for (i32 z = -1 * CHUNK_RENDER_DISTANCE_PER_DIMENSION_EXTENT; z <= (i32)CHUNK_RENDER_DISTANCE_PER_DIMENSION_EXTENT;
         z++)
    {
        for (i32 y = -CHUNK_RENDER_DISTANCE_PER_DIMENSION_EXTENT; y <= (i32)CHUNK_RENDER_DISTANCE_PER_DIMENSION_EXTENT;
             y++)
        {
            for (i32 x = -CHUNK_RENDER_DISTANCE_PER_DIMENSION_EXTENT;
                 x <= (i32)CHUNK_RENDER_DISTANCE_PER_DIMENSION_EXTENT; x++)
            {
                if ((z == -CHUNK_RENDER_DISTANCE_PER_DIMENSION_EXTENT ||
                     z == CHUNK_RENDER_DISTANCE_PER_DIMENSION_EXTENT) ||
                    (y == -CHUNK_RENDER_DISTANCE_PER_DIMENSION_EXTENT ||
                     y == CHUNK_RENDER_DISTANCE_PER_DIMENSION_EXTENT) ||
                    (x == -CHUNK_RENDER_DISTANCE_PER_DIMENSION_EXTENT || x == CHUNK_RENDER_DISTANCE_PER_DIMENSION))
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

    camera_t camera{};

    timer_t timer{};
    f32 delta_time = 0.0f;

    b32 setup_chunks{false};

    u64 frame_count = 0;

    b32 quit{false};
    while (!quit)
    {
        static f32 near_plane = 1.0f;

        // Get the player's current chunk index.
        const voxel_chunk_position_t current_chunk_3d_index = {
            (i32)(floor((camera.m_position.x) / voxel_chunk_t::CHUNK_LENGTH)),
            (i32)(floor((camera.m_position.y) / voxel_chunk_t::CHUNK_LENGTH)),
            (i32)(floor((camera.m_position.z) / voxel_chunk_t::CHUNK_LENGTH)),
        };

        // Evict the chunks that are out of range of render distance.
        std::erase_if(chunk_manager.m_loaded_chunks, [current_chunk_3d_index](
                                                         const std::pair<const voxel_chunk_position_t, voxel_chunk_t>
                                                             &key_value_pair) {
            const auto &chunk = key_value_pair.second;

            if ((std::abs(chunk.m_chunk_position.x - current_chunk_3d_index.x) > CHUNK_RENDER_DISTANCE_PER_DIMENSION) ||
                (std::abs(chunk.m_chunk_position.y - current_chunk_3d_index.y) > CHUNK_RENDER_DISTANCE_PER_DIMENSION) ||
                (std::abs(chunk.m_chunk_position.z - current_chunk_3d_index.z) > CHUNK_RENDER_DISTANCE_PER_DIMENSION))
            {
                return true;
            }

            return false;
        });

        if (setup_chunks)
        {
            // Load chunks around the player.
            for (const auto &offset : chunk_render_distance_offsets)
            {
                const voxel_chunk_position_t chunk_3d_index = {
                    current_chunk_3d_index.x + offset.x,
                    current_chunk_3d_index.y + offset.y,
                    current_chunk_3d_index.z + offset.z,
                };

                chunk_manager.add_chunk_to_setup_stack(chunk_3d_index);
            }
        }

        chunk_manager.create_chunks_from_setup_stack(renderer);

        u8 keyboard_state[256] = {};

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

        b32 get_keyboard_state_result = GetKeyboardState(keyboard_state);
        assert(get_keyboard_state_result);

        chunk_manager.transfer_chunks_from_setup_to_loaded_state(renderer.m_copy_queue.m_fence->GetCompletedValue());

        const float window_aspect_ratio = static_cast<float>(window.get_width()) / window.get_height();

        rhi::constant_buffer_t<interop::scene_constant_buffer_t> &scene_buffer =
            scene_buffers[renderer.m_swapchain_backbuffer_index];

        DirectX::XMMATRIX projection_matrix = {};
        {
            // Article followed for reverse Z:
            //  https://iolite-engine.com/blog_posts/reverse_z_cheatsheet

            // https://github.com/microsoft/DirectXMath/issues/158 link that shows the projection matrix for infinite
            // far plane. Note : This code is taken from the directxmath source code for perspective projection fov lh,
            // but modified for infinite far plane.

            float sin_fov{};
            float cos_fov{};
            DirectX::XMScalarSinCos(&sin_fov, &cos_fov, 0.5f * DirectX::XMConvertToRadians(45.0f));

            float height = cos_fov / sin_fov;
            float width = height / window_aspect_ratio;

            projection_matrix = DirectX::XMMatrixSet(width, 0.0f, 0.0f, 0.0f, 0.0f, height, 0.0f, 0.0f, 0.0f, 0.0f,
                                                     0.0f, 1.0f, 0.0f, 0.0f, near_plane, 0.0f);
        }
        scene_buffer.m_data.view_matrix = camera.update_and_get_view_matrix(keyboard_state, delta_time);
        scene_buffer.m_data.projection_matrix = projection_matrix;
        scene_buffer.m_data.camera_position = camera.m_position;
        scene_buffer.m_data.voxel_chunk_length = voxel_chunk_t::CHUNK_LENGTH;
        scene_buffer.update();

        const auto &swapchain_index = renderer.m_swapchain_backbuffer_index;

        // Reset command allocator and command list.
        renderer.m_direct_queue.reset(swapchain_index);

        const auto &command_list = renderer.m_direct_queue.m_command_list;

        const auto &swapchain_backbuffer = renderer.m_swapchain_backbuffers[swapchain_index];

        const ComPtr<ID3D12Resource> swapchain_resource = swapchain_backbuffer.m_resource;

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
        command_list->ClearRenderTargetView(swapchain_backbuffer.m_rtv_cpu_descriptor_handle, clear_color, 0u, nullptr);
        command_list->ClearDepthStencilView(dsv_handle, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0u, 0u, nullptr);

        // Set viewport.
        command_list->RSSetViewports(1u, &viewport);
        command_list->RSSetScissorRects(1u, &scissor_rect);

        // Setup indirect command vector.
        indirect_command_vector.clear();
        for (const auto &[chunk_position, chunk] : chunk_manager.m_loaded_chunks)
        {
            const interop::voxel_render_resources_t render_resources = {
                .scene_constant_buffer_index = scene_buffer.m_cbv_index,
                .shared_chunk_position_buffer_index = chunk_manager.m_shared_chunk_position_buffer.m_srv_index,
                .color_buffer_index = chunk.m_color_buffer.m_srv_index,
                .chunk_position = {chunk_position.x, chunk_position.y, chunk_position.z},
            };

            indirect_command_vector.emplace_back(indirect_command_t{
                .render_resources = render_resources,
                .index_buffer_view = chunk.m_index_buffer.m_index_buffer_view,
                .draw_arguments =
                    D3D12_DRAW_INDEXED_ARGUMENTS{
                        .IndexCountPerInstance = (u32)chunk.m_index_buffer.m_indices_count,
                        .InstanceCount = 1u,
                        .StartIndexLocation = 0u,
                        .BaseVertexLocation = 0u,
                        .StartInstanceLocation = 0u,
                    },
            });
        }

        ID3D12DescriptorHeap *const *shader_visible_descriptor_heaps = {
            renderer.m_cbv_srv_uav_descriptor_heap.m_descriptor_heap.GetAddressOf(),
        };

        command_list->SetDescriptorHeaps(1u, shader_visible_descriptor_heaps);

        // Prepare rendering commands.
        command_list->OMSetRenderTargets(1u, &swapchain_backbuffer.m_rtv_cpu_descriptor_handle, FALSE, &dsv_handle);

        // Run the culling compute shader, followed by voxel rendering shader.
        if (!indirect_command_vector.empty())
        {
            const D3D12_RESOURCE_BARRIER indirect_argument_to_copy_dest_state = {
                .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                .Flags = D3D12_RESOURCE_BARRIER_FLAGS::D3D12_RESOURCE_BARRIER_FLAG_NONE,
                .Transition =
                    D3D12_RESOURCE_TRANSITION_BARRIER{
                        .pResource = indirect_command_buffer.m_default_resource.Get(),
                        .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                        .StateBefore = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
                        .StateAfter = D3D12_RESOURCE_STATE_COPY_DEST,
                    },
            };
            command_list->ResourceBarrier(1u, &indirect_argument_to_copy_dest_state);

            memcpy(indirect_command_buffer.m_upload_resource_mapped_ptr, indirect_command_vector.data(),
                   indirect_command_vector.size() * sizeof(indirect_command_t));

            interop::gpu_cull_render_resources_t gpu_cull_render_resources = {
                .number_of_chunks = (u32)indirect_command_vector.size(),
                .indirect_command_srv_index = indirect_command_buffer.m_upload_resource_srv_index,
                .output_command_uav_index = indirect_command_buffer.m_default_resource_uav_index,
                .scene_constant_buffer_index = scene_buffer.m_cbv_index,
            };

            command_list->SetDescriptorHeaps(1u, shader_visible_descriptor_heaps);
            command_list->SetComputeRootSignature(renderer.m_bindless_root_signature.Get());
            command_list->SetPipelineState(gpu_culling_pso.Get());

            command_list->SetComputeRoot32BitConstants(0u, 64u, &gpu_cull_render_resources, 0u);

            // Clear the counter associated with UAV.

            command_list->CopyBufferRegion(indirect_command_buffer.m_default_resource.Get(),
                                           indirect_command_buffer.m_counter_offset,
                                           indirect_command_buffer.m_zeroed_counter_buffer_resource.Get(), 0u, 4u);

            const D3D12_RESOURCE_BARRIER copy_dest_to_unordered_access_state = {
                .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                .Flags = D3D12_RESOURCE_BARRIER_FLAGS::D3D12_RESOURCE_BARRIER_FLAG_NONE,
                .Transition =
                    D3D12_RESOURCE_TRANSITION_BARRIER{
                        .pResource = indirect_command_buffer.m_default_resource.Get(),
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
                        .pResource = indirect_command_buffer.m_default_resource.Get(),
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
                command_signature.Get(), MAX_CHUNKS_TO_BE_DRAWN, indirect_command_buffer.m_default_resource.Get(), 0u,
                indirect_command_buffer.m_default_resource.Get(), indirect_command_buffer.m_counter_offset);
        }

        // Render UI.
        // Start the Dear ImGui frame

        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Debug Controller");
        ImGui::SliderFloat("movement_speed", &camera.m_movement_speed, 0.0f, 500000.0f);
        ImGui::SliderFloat("rotation_speed", &camera.m_rotation_speed, 0.0f, 10.0f);
        ImGui::SliderFloat("friction", &camera.m_friction, 0.0f, 1.0f);
        ImGui::SliderFloat("near plane", &near_plane, 0.1f, 1.0f);
        ImGui::Checkbox("Start loading chunks", &setup_chunks);
        ImGui::Text("Delta Time: %f", delta_time);
        ImGui::Text("Camera Position : %f %f %f", camera.m_position.x, camera.m_position.y, camera.m_position.z);
        ImGui::Text("Pitch and Yaw: %f %f", camera.m_pitch, camera.m_yaw);
        ImGui::Text("Current 3D Index: %d, %d, %d", current_chunk_3d_index.x, current_chunk_3d_index.y,
                    current_chunk_3d_index.z);
        ImGui::Text("Number of loaded chunks: %zu", chunk_manager.m_loaded_chunks.size());
        ImGui::Text("Number of copy alloc / list pairs : %zu",
                    renderer.m_copy_queue.m_command_allocator_list_queue.size());
        ImGui::Text("Voxel edge length : %zu", voxel_t::EDGE_LENGTH);
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

        delta_time = timer.tick_and_get_delta_time_seconds();
    }

    // Cleanup
    renderer.m_direct_queue.flush_queue();
    renderer.m_copy_queue.flush_queue();

    return 0;
}
