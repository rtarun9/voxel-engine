#include "voxel-engine/camera.hpp"
#include "voxel-engine/filesystem.hpp"
#include "voxel-engine/renderer.hpp"
#include "voxel-engine/shader_compiler.hpp"
#include "voxel-engine/timer.hpp"
#include "voxel-engine/window.hpp"

#include "shaders/interop/render_resources.hlsli"

#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

// Helper functions to go from 1d to 3d and vice versa.
static inline size_t convert_to_1d(const DirectX::XMUINT3 index_3d, const size_t N)
{
    return index_3d.x + N * (index_3d.y + index_3d.z * N);
}

static inline DirectX::XMUINT3 convert_to_3d(const size_t index, const size_t N)
{
    // For reference, index = x + y * N + z * N * N.
    const u32 z = static_cast<u32>(index / (N * N));
    const u32 index_2d = static_cast<u32>(index - z * N * N);
    const u32 y = static_cast<u32>(index_2d / N);
    const u32 x = static_cast<u32>(index_2d % N);

    return {x, y, z};
}

// A voxel is just a value on a regular 3D grid. Think of it as the corners where the cells meet in a 3d grid.
// For 3d visualization of voxels, A cube is rendered for each voxel where the front lower left corner is the 'voxel
// position' and has a edge length as specified in the class below.
struct Voxel
{
    static constexpr float EDGE_LENGTH{0.1f};
    bool m_active{true};
};

struct Chunk
{
    explicit Chunk()
    {
        m_voxels = new Voxel[NUMBER_OF_VOXELS];
    }

    ~Chunk()
    {
        delete[] m_voxels;
    }

    static constexpr u32 NUMBER_OF_VOXELS_PER_DIMENSION = 9u;
    static constexpr size_t NUMBER_OF_VOXELS =
        NUMBER_OF_VOXELS_PER_DIMENSION * NUMBER_OF_VOXELS_PER_DIMENSION * NUMBER_OF_VOXELS_PER_DIMENSION;

    // A flattened 3d array of Voxels.
    Voxel *m_voxels{};
    size_t m_chunk_index{};
};

// A class that contains a collection of chunks with data for each stored in hash maps for quick access.

// Explanation for the asynchronous chunk loading system.
// 2 queues are used, a direct and upload. The upload queue is required because to copy data from upload to GPU only
// buffer, we need to submit a command list.
// Also, during this process the CPU side data must be present. So, what is done is that we keep the CPU side buffer
// data until the upload buffer has been submitted and the buffers have been created.

// std::async is used to call the setup chunk function. This function will perform the meshing algoritihm. Once the
// future is ready, we move the data into another vector (chunks_to_load), and once the upload queue has finished
// execution, move those chunks into loaded chunks hashmap.
struct ChunkManager
{
    struct ChunkMeshData
    {
        std::vector<DirectX::XMFLOAT3> position_buffer_data{};
        DirectX::XMFLOAT3 chunk_color_data{};
        size_t chunk_index{};
    };

  private:
    static ChunkMeshData setup_chunk_mesh(const size_t index)
    {
        // Iterate over each voxel in chunk and setup the chunk common position buffer.
        std::vector<DirectX::XMFLOAT3> position_data{};

        static constexpr std::array<DirectX::XMFLOAT3, 8> voxel_vertices_data{
            DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f),
            DirectX::XMFLOAT3(0.0f, Voxel::EDGE_LENGTH, 0.0f),
            DirectX::XMFLOAT3(Voxel::EDGE_LENGTH, Voxel::EDGE_LENGTH, 0.0f),
            DirectX::XMFLOAT3(Voxel::EDGE_LENGTH, 0.0f, 0.0f),
            DirectX::XMFLOAT3(0.0f, 0.0f, Voxel::EDGE_LENGTH),
            DirectX::XMFLOAT3(0.0f, Voxel::EDGE_LENGTH, Voxel::EDGE_LENGTH),
            DirectX::XMFLOAT3(Voxel::EDGE_LENGTH, Voxel::EDGE_LENGTH, Voxel::EDGE_LENGTH),
            DirectX::XMFLOAT3(Voxel::EDGE_LENGTH, 0.0f, Voxel::EDGE_LENGTH),
        };

        const DirectX::XMUINT3 chunk_index_3d = convert_to_3d(index, ChunkManager::NUMBER_OF_CHUNKS_PER_DIMENSION);
        const DirectX::XMFLOAT3 chunk_offset =
            DirectX::XMFLOAT3(chunk_index_3d.x * Voxel::EDGE_LENGTH * Chunk::NUMBER_OF_VOXELS_PER_DIMENSION,
                              chunk_index_3d.y * Voxel::EDGE_LENGTH * Chunk::NUMBER_OF_VOXELS_PER_DIMENSION,
                              chunk_index_3d.z * Voxel::EDGE_LENGTH * Chunk::NUMBER_OF_VOXELS_PER_DIMENSION);

        for (size_t i = 0; i < Chunk::NUMBER_OF_VOXELS; i++)
        {
            const DirectX::XMUINT3 index_3d = convert_to_3d(i, Chunk::NUMBER_OF_VOXELS_PER_DIMENSION);
            const DirectX::XMFLOAT3 offset = DirectX::XMFLOAT3(index_3d.x * Voxel::EDGE_LENGTH + chunk_offset.x,
                                                               index_3d.y * Voxel::EDGE_LENGTH + chunk_offset.y,
                                                               index_3d.z * Voxel::EDGE_LENGTH + chunk_offset.z);

            // Check if there is a voxel that blocks the front face of current voxel.
            {

                const bool is_front_face_covered =
                    (index_3d.z != 0 && m_chunks[index]
                                            .m_voxels[convert_to_1d({index_3d.x, index_3d.y, index_3d.z - 1},
                                                                    Chunk::NUMBER_OF_VOXELS_PER_DIMENSION)]
                                            .m_active);

                if (!is_front_face_covered)
                {
                    for (const auto &vertex_index : {0u, 1u, 2u, 0u, 2u, 3u})
                    {
                        const auto &vertex = voxel_vertices_data[vertex_index];
                        position_data.emplace_back(
                            DirectX::XMFLOAT3{vertex.x + offset.x, vertex.y + offset.y, vertex.z + offset.z});
                    }
                }
            }

            // Check if there is a voxel that blocks the back face of current voxel.
            {

                const bool is_back_face_covered = (index_3d.z != Chunk::NUMBER_OF_VOXELS_PER_DIMENSION - 1u &&
                                                   m_chunks[index]
                                                       .m_voxels[convert_to_1d({index_3d.x, index_3d.y, index_3d.z + 1},
                                                                               Chunk::NUMBER_OF_VOXELS_PER_DIMENSION)]
                                                       .m_active);

                if (!is_back_face_covered)
                {
                    for (const auto &vertex_index : {4u, 6u, 5u, 4u, 7u, 6u})
                    {
                        const auto &vertex = voxel_vertices_data[vertex_index];
                        position_data.emplace_back(
                            DirectX::XMFLOAT3{vertex.x + offset.x, vertex.y + offset.y, vertex.z + offset.z});
                    }
                }
            }

            // Check if there is a voxel that blocks the left hand side face of current voxel.
            {

                const bool is_left_face_covered =
                    (index_3d.x != 0u && m_chunks[index]
                                             .m_voxels[convert_to_1d({index_3d.x - 1, index_3d.y, index_3d.z},
                                                                     Chunk::NUMBER_OF_VOXELS_PER_DIMENSION)]
                                             .m_active);

                if (!is_left_face_covered)
                {
                    for (const auto &vertex_index : {4u, 5u, 1u, 4u, 1u, 0u})
                    {
                        const auto &vertex = voxel_vertices_data[vertex_index];
                        position_data.emplace_back(
                            DirectX::XMFLOAT3{vertex.x + offset.x, vertex.y + offset.y, vertex.z + offset.z});
                    }
                }
            }

            // Check if there is a voxel that blocks the right hand side face of current voxel.
            {

                const bool is_right_face_covered =
                    (index_3d.x != Chunk::NUMBER_OF_VOXELS_PER_DIMENSION - 1u &&
                     m_chunks[index]
                         .m_voxels[convert_to_1d({index_3d.x + 1, index_3d.y, index_3d.z},
                                                 Chunk::NUMBER_OF_VOXELS_PER_DIMENSION)]
                         .m_active);

                if (!is_right_face_covered)
                {
                    for (const auto &vertex_index : {3u, 2u, 6u, 3u, 6u, 7u})
                    {
                        const auto &vertex = voxel_vertices_data[vertex_index];
                        position_data.emplace_back(
                            DirectX::XMFLOAT3{vertex.x + offset.x, vertex.y + offset.y, vertex.z + offset.z});
                    }
                }
            }

            // Check if there is a voxel that blocks the top side face of current voxel.
            {

                const bool is_top_face_covered = (index_3d.y != Chunk::NUMBER_OF_VOXELS_PER_DIMENSION - 1 &&
                                                  m_chunks[index]
                                                      .m_voxels[convert_to_1d({index_3d.x, index_3d.y + 1, index_3d.z},
                                                                              Chunk::NUMBER_OF_VOXELS_PER_DIMENSION)]
                                                      .m_active);

                if (!is_top_face_covered)
                {
                    for (const auto &vertex_index : {1u, 5u, 6u, 1u, 6u, 2u})
                    {
                        const auto &vertex = voxel_vertices_data[vertex_index];
                        position_data.emplace_back(
                            DirectX::XMFLOAT3{vertex.x + offset.x, vertex.y + offset.y, vertex.z + offset.z});
                    }
                }
            }

            // Check if there is a voxel that blocks the bottom side face of current voxel.
            {

                const bool is_bottom_face_covered =
                    (index_3d.y != 0u && m_chunks[index]
                                             .m_voxels[convert_to_1d({index_3d.x, index_3d.y - 1, index_3d.z},
                                                                     Chunk::NUMBER_OF_VOXELS_PER_DIMENSION)]
                                             .m_active);

                if (!is_bottom_face_covered)
                {
                    for (const auto &vertex_index : {4u, 0u, 3u, 4u, 3u, 7u})
                    {
                        const auto &vertex = voxel_vertices_data[vertex_index];
                        position_data.emplace_back(
                            DirectX::XMFLOAT3{vertex.x + offset.x, vertex.y + offset.y, vertex.z + offset.z});
                    }
                }
            }
        }

        const DirectX::XMFLOAT3 chunk_color_data = {
            rand() / (float)RAND_MAX,
            rand() / (float)RAND_MAX,
            rand() / (float)RAND_MAX,
        };

        return ChunkMeshData{
            .position_buffer_data = std::move(position_data),
            .chunk_color_data = chunk_color_data,
            .chunk_index = index,
        };
    }

  public:
    // When a future is ready, the GPU side buffers need to be allocated.
    void setup_chunk(const Renderer &renderer, const size_t index)
    {
        m_setup_chunk_mesh_data.push(std::async(std::launch::async, &ChunkManager::_setup_chunk, index));
    }

    void create_chunk_from_mesh_data(Renderer &renderer, const ChunkMeshData &mesh_data)
    {
        const size_t index = mesh_data.chunk_index;

        if (!mesh_data.position_buffer_data.empty())
        {
            m_chunk_position_buffers[index] =
                renderer.create_structured_buffer((void *)mesh_data.position_buffer_data.data(),
                                                  sizeof(DirectX::XMFLOAT3), mesh_data.position_buffer_data.size());
            m_chunk_color_buffers[index] =
                renderer.create_structured_buffer((void *)&mesh_data.chunk_color_data, sizeof(DirectX::XMFLOAT3), 1u);
        }

        m_chunk_number_of_vertices[index] = mesh_data.position_buffer_data.size();

        m_chunks[index].m_chunk_index = index;
    }

    static constexpr u32 NUMBER_OF_CHUNKS_PER_DIMENSION = 5u;
    static constexpr size_t NUMBER_OF_CHUNKS =
        NUMBER_OF_CHUNKS_PER_DIMENSION * NUMBER_OF_CHUNKS_PER_DIMENSION * NUMBER_OF_CHUNKS_PER_DIMENSION;

    // Data for the 'loaded' chunks.
    std::unordered_map<size_t, Chunk> m_chunks{};

    std::unordered_map<size_t, StructuredBuffer> m_chunk_position_buffers{};
    std::unordered_map<size_t, StructuredBuffer> m_chunk_color_buffers{};
    std::unordered_map<size_t, size_t> m_chunk_number_of_vertices{};

    // Data for the 'to setup' chunks.
    std::stack<std::future<ChunkMeshData>> m_setup_chunk_mesh_data{};
};

int main()
{
    printf("Executable path :: %s\n", FileSystem::instance().executable_path().c_str());

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

    ChunkManager chunk_manager{};

    for (size_t i = 0; i < ChunkManager::NUMBER_OF_CHUNKS; i++)
    {
        // chunk_manager.create_chunk_from_mesh_data(renderer, i);
        chunk_manager.m_chunk_mesh_data.push(
            std::async(std::launch::async, &ChunkManager::get_chunk_mesh, chunk_manager, i));
    }

    SceneConstantBuffer scene_buffer_data{};
    ConstantBuffer scene_buffer = renderer.create_constant_buffer(sizeof(SceneConstantBuffer));

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
        .DepthStencil = {.Depth = 1.0f, .Stencil = 1u},
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
                            .RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL,
                        },
                    },
            },
        .SampleMask = 0xffff'ffff,
        .RasterizerState =
            {
                .FillMode = D3D12_FILL_MODE_WIREFRAME,
                .CullMode = D3D12_CULL_MODE_BACK,
                .FrontCounterClockwise = FALSE,
            },
        .DepthStencilState =
            {
                .DepthEnable = TRUE,
                .DepthWriteMask = D3D12_DEPTH_WRITE_MASK::D3D12_DEPTH_WRITE_MASK_ALL,
                .DepthFunc = D3D12_COMPARISON_FUNC_LESS,
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
    renderer.execute_command_list();
    renderer.flush_gpu();

    Camera camera{};
    Timer timer{};
    float delta_time = 0.0f;

    u64 frame_count = 0;

    bool quit{false};
    while (!quit)
    {
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

        const float window_aspect_ratio = static_cast<float>(window.get_width()) / window.get_height();

        const DirectX::XMMATRIX view_projection_matrix =
            camera.update_and_get_view_matrix(delta_time) *
            DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(45.0f), window_aspect_ratio, 0.1f, 10.0f);
        scene_buffer_data.view_projection_matrix = view_projection_matrix;

        scene_buffer.update(&scene_buffer_data);

        const auto &allocator = renderer.m_direct_command_allocators[renderer.m_swapchain_backbuffer_index];
        const auto &command_list = renderer.m_command_list;

        const auto &swapchain_index = renderer.m_swapchain_backbuffer_index;

        // Reset command allocator and command list.
        throw_if_failed(allocator->Reset());
        throw_if_failed(command_list->Reset(allocator.Get(), nullptr));

        const auto &rtv_handle = renderer.m_swapchain_backbuffer_cpu_descriptor_handles[swapchain_index];
        const Microsoft::WRL::ComPtr<ID3D12Resource> &swapchain_resource = renderer.m_resources[swapchain_index];

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
        const float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        command_list->ClearRenderTargetView(rtv_handle, clear_color, 0u, nullptr);
        command_list->ClearDepthStencilView(dsv_handle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 1u, 0u,
                                            nullptr);

        command_list->OMSetRenderTargets(1u, &rtv_handle, FALSE, &dsv_handle);

        // Set viewport.
        command_list->RSSetViewports(1u, &viewport);
        command_list->RSSetScissorRects(1u, &scissor_rect);

        // Start the Dear ImGui frame
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ID3D12DescriptorHeap *const *shader_visible_descriptor_heaps = {
            renderer.m_cbv_srv_uav_descriptor_heap.descriptor_heap.GetAddressOf(),
        };

        command_list->SetDescriptorHeaps(1u, shader_visible_descriptor_heaps);

        // Set the index buffer, pso and all config settings for rendering.
        command_list->SetGraphicsRootSignature(renderer.m_bindless_root_signature.Get());
        command_list->SetPipelineState(pso.Get());

        command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        for (size_t k = 0; k < chunk_manager.m_chunks.size(); k++)
        {
            const size_t i = chunk_manager.m_chunks[k].m_chunk_index;

            const VoxelRenderResources render_resources = {
                .position_buffer_index = static_cast<u32>(chunk_manager.m_chunk_position_buffers[i].srv_index),
                .color_buffer_index = static_cast<u32>(chunk_manager.m_chunk_color_buffers[i].srv_index),
                .chunk_index = static_cast<u32>(i),
                .scene_constant_buffer_index = static_cast<u32>(scene_buffer.cbv_index),
            };

            command_list->SetGraphicsRoot32BitConstants(0u, 64u, &render_resources, 0u);
            command_list->DrawInstanced((u32)chunk_manager.m_chunk_number_of_vertices[i], 1u, 0u, 0u);
        }

        // Render UI.
        ImGui::Begin("Debug Controller");
        ImGui::SliderFloat("movement_speed", &camera.m_movement_speed, 0.0f, 10.0f);
        ImGui::SliderFloat("rotation_speed", &camera.m_rotation_speed, 0.0f, 10.0f);
        ImGui::End();

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
        renderer.execute_command_list();

        // Now, present the rendertarget and signal command queue.
        throw_if_failed(renderer.m_swapchain->Present(1u, 0u));
        renderer.signal_fence();

        renderer.m_swapchain_backbuffer_index = static_cast<u8>(renderer.m_swapchain->GetCurrentBackBufferIndex());

        // Wait for the previous frame (that is presenting to swpachain_backbuffer_index) to complete execution.
        renderer.wait_for_fence_value_at_index(renderer.m_swapchain_backbuffer_index);

        ++frame_count;

        timer.stop();
        delta_time = timer.get_delta_time();
    }

    // Cleanup
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    renderer.flush_gpu();

    return 0;
}