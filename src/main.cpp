#include "pch.hpp"

#include "renderer.hpp"
#include "types.hpp"
#include "window.hpp"

#include "common.hpp"

#include "camera.hpp"

#include "voxel.hpp"

// Helper function for indexing.
static inline DirectX::XMUINT3 convert_index_to_3d(const u64 index)
{
    // Note that index = x + y * N + z * N * N;
    const u32 z = index / (Chunk::number_of_voxels_per_dimension * Chunk::number_of_voxels_per_dimension);
    const u32 index_2d = index - z * (Chunk::number_of_voxels_per_dimension * Chunk::number_of_voxels_per_dimension);
    const u32 y = index_2d / Chunk::number_of_voxels_per_dimension;
    const u32 x = index_2d % Chunk::number_of_voxels_per_dimension;

    return DirectX::XMUINT3{x, y, z};
}

int main()
{
    const Window window(1080u, 720u);
    Renderer renderer(window.m_handle, window.m_width, window.m_height);

    // A vector of intermediate resources (required since intermediate buffers need to be in memory until the copy
    // resource and other functions are executed).
    // This vector MUST be erased once GPU resource uploading is completed.
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> intermediate_resources{};
    intermediate_resources.reserve(50);

    // Create the resources required for rendering.

    // Vertex buffer setup.
    struct VertexData
    {
        DirectX::XMFLOAT3 position{};
        DirectX::XMFLOAT3 color{};
    };

    static constexpr float voxel_cube_dimension = 0.25f;

    // Note : Each chunk has its own vertex buffer.
    // The renderer has an array of vertex buffers to simply the process of rendering.
    std::vector<VertexData> chunk_vertex_data{};
    Chunk chunk{};

    // Set voxels in chunk to active (true / false) here.
    for (u64 i = 0; i < Chunk::number_of_voxels_per_dimension * Chunk::number_of_voxels_per_dimension *
                            Chunk::number_of_voxels_per_dimension;
         i++)
    {
    }

    // Loop over chunks here...
    for (u64 i = 0; i < Chunk::number_of_voxels_per_dimension * Chunk::number_of_voxels_per_dimension *
                            Chunk::number_of_voxels_per_dimension;
         i++)
    {
        // Vertex buffer construction.
        const DirectX::XMUINT3 index = convert_index_to_3d(i);
        if (chunk.cubes[i].active)
        {
            const DirectX::XMFLOAT3 position_offset =
                DirectX::XMFLOAT3{voxel_cube_dimension * (float)index.x, voxel_cube_dimension * (float)index.y,
                                  voxel_cube_dimension * (float)index.z};
            const float voxel_render_size = voxel_cube_dimension * 0.5f;

            const VertexData v1 = (VertexData{.position = DirectX::XMFLOAT3(position_offset.x - voxel_render_size,
                                                                            position_offset.y - voxel_render_size,
                                                                            position_offset.z - voxel_render_size),
                                              .color = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f)});
            const VertexData v2 = (VertexData{.position = DirectX::XMFLOAT3(position_offset.x - voxel_render_size,
                                                                            position_offset.y + voxel_render_size,
                                                                            position_offset.z - voxel_render_size),
                                              .color = DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f)});
            const VertexData v3 = (VertexData{.position = DirectX::XMFLOAT3(position_offset.x + voxel_render_size,
                                                                            position_offset.y + voxel_render_size,
                                                                            position_offset.z - voxel_render_size),
                                              .color = DirectX::XMFLOAT3(1.0f, 1.0f, 0.0f)});
            const VertexData v4 = (VertexData{.position = DirectX::XMFLOAT3(position_offset.x + voxel_render_size,
                                                                            position_offset.y - voxel_render_size,
                                                                            position_offset.z - voxel_render_size),
                                              .color = DirectX::XMFLOAT3(1.0f, 0.0f, 0.0f)});
            const VertexData v5 = (VertexData{.position = DirectX::XMFLOAT3(position_offset.x - voxel_render_size,
                                                                            position_offset.y - voxel_render_size,
                                                                            position_offset.z + voxel_render_size),
                                              .color = DirectX::XMFLOAT3(0.0f, 0.0f, 1.0f)});
            const VertexData v6 = (VertexData{.position = DirectX::XMFLOAT3(position_offset.x - voxel_render_size,
                                                                            position_offset.y + voxel_render_size,
                                                                            position_offset.z + voxel_render_size),
                                              .color = DirectX::XMFLOAT3(0.0f, 1.0f, 1.0f)});
            const VertexData v7 = (VertexData{.position = DirectX::XMFLOAT3(position_offset.x + voxel_render_size,
                                                                            position_offset.y + voxel_render_size,
                                                                            position_offset.z + voxel_render_size),
                                              .color = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f)});
            const VertexData v8 = (VertexData{.position = DirectX::XMFLOAT3(position_offset.x + voxel_render_size,
                                                                            position_offset.y - voxel_render_size,
                                                                            position_offset.z + voxel_render_size),
                                              .color = DirectX::XMFLOAT3(1.0f, 0.0f, 1.0f)});

            chunk_vertex_data.reserve(chunk_vertex_data.size() + 36);

            chunk_vertex_data.emplace_back(v1);
            chunk_vertex_data.emplace_back(v2);
            chunk_vertex_data.emplace_back(v3);

            chunk_vertex_data.emplace_back(v1);
            chunk_vertex_data.emplace_back(v3);
            chunk_vertex_data.emplace_back(v4);

            chunk_vertex_data.emplace_back(v5);
            chunk_vertex_data.emplace_back(v7);
            chunk_vertex_data.emplace_back(v6);

            chunk_vertex_data.emplace_back(v5);
            chunk_vertex_data.emplace_back(v8);
            chunk_vertex_data.emplace_back(v7);

            chunk_vertex_data.emplace_back(v5);
            chunk_vertex_data.emplace_back(v6);
            chunk_vertex_data.emplace_back(v2);

            chunk_vertex_data.emplace_back(v5);
            chunk_vertex_data.emplace_back(v2);
            chunk_vertex_data.emplace_back(v1);

            chunk_vertex_data.emplace_back(v4);
            chunk_vertex_data.emplace_back(v3);
            chunk_vertex_data.emplace_back(v7);

            chunk_vertex_data.emplace_back(v4);
            chunk_vertex_data.emplace_back(v7);
            chunk_vertex_data.emplace_back(v8);

            chunk_vertex_data.emplace_back(v2);
            chunk_vertex_data.emplace_back(v6);
            chunk_vertex_data.emplace_back(v7);

            chunk_vertex_data.emplace_back(v2);
            chunk_vertex_data.emplace_back(v7);
            chunk_vertex_data.emplace_back(v3);

            chunk_vertex_data.emplace_back(v5);
            chunk_vertex_data.emplace_back(v1);
            chunk_vertex_data.emplace_back(v4);

            chunk_vertex_data.emplace_back(v5);
            chunk_vertex_data.emplace_back(v4);
            chunk_vertex_data.emplace_back(v8);
        }
    };

    Renderer::BufferPair vertex_buffer_pair = renderer.create_buffer(
        (void *)chunk_vertex_data.data(), sizeof(VertexData) * chunk_vertex_data.size(), BufferTypes::Static);

    intermediate_resources.emplace_back(vertex_buffer_pair.intermediate_buffer.buffer);
    Buffer vertex_buffer = vertex_buffer_pair.buffer;

    // Create the vertex buffer view.
    D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view = {
        .BufferLocation = vertex_buffer.buffer->GetGPUVirtualAddress(),
        .SizeInBytes = (u32)(sizeof(VertexData) * chunk_vertex_data.size()),
        .StrideInBytes = sizeof(VertexData),
    };

    // Create a 'scene' constant buffer for simple linear algebra tests.
    struct alignas(256) SceneConstantBuffer
    {
        DirectX::XMMATRIX view_projection_matrix{};
    };

    Buffer scene_constant_buffer =
        renderer.create_buffer(nullptr, sizeof(SceneConstantBuffer), BufferTypes::Dynamic).buffer;

    // Create the scene constant buffer descriptor.
    const D3D12_CPU_DESCRIPTOR_HANDLE scene_constant_buffer_descriptor_handle =
        renderer.create_constant_buffer_view(scene_constant_buffer, sizeof(SceneConstantBuffer));
    (void)scene_constant_buffer_descriptor_handle;

    // Create the depth stencil buffer.
    // This process is not abstracted because depth resource will not be created multiple times.
    // If in future this happens, a create_xyz function will be created in renderer.
    Microsoft::WRL::ComPtr<ID3D12Resource> depth_resource{};
    D3D12_CPU_DESCRIPTOR_HANDLE dsv_cpu_handle = renderer.dsv_descriptor_heap.current_cpu_descriptor_handle;
    renderer.dsv_descriptor_heap.offset();
    {
        const D3D12_RESOURCE_DESC ds_resource_desc = {
            .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
            .Alignment = 0u,
            .Width = window.m_width,
            .Height = window.m_height,
            .DepthOrArraySize = 1u,
            .MipLevels = 1u,
            .Format = DXGI_FORMAT_D32_FLOAT,
            .SampleDesc =
                {
                    1u,
                    0u,
                },
            .Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
            .Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
        };

        const D3D12_HEAP_PROPERTIES default_heap_properties = {
            .Type = D3D12_HEAP_TYPE_DEFAULT,
            .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
            .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
            .CreationNodeMask = 0u,
            .VisibleNodeMask = 0u,
        };

        const D3D12_CLEAR_VALUE optimized_ds_clear_value = {
            .Format = DXGI_FORMAT_D32_FLOAT,
            .DepthStencil =
                {
                    .Depth = 1.0f,
                },
        };

        throw_if_failed(renderer.device->CreateCommittedResource(
            &default_heap_properties, D3D12_HEAP_FLAG_NONE, &ds_resource_desc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &optimized_ds_clear_value, IID_PPV_ARGS(&depth_resource)));

        const D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {
            .Format = DXGI_FORMAT_D32_FLOAT,
            .ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D,
            .Flags = D3D12_DSV_FLAG_NONE,
            .Texture2D =
                {

                    .MipSlice = 0u,
                },
        };

        renderer.device->CreateDepthStencilView(depth_resource.Get(), &dsv_desc, dsv_cpu_handle);
    }

    Microsoft::WRL::ComPtr<ID3DBlob> root_signature_blob{};
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature{};

    D3D12_ROOT_PARAMETER1 root_parameters[1] = {
        // For scene constant buffer.
        D3D12_ROOT_PARAMETER1{
            .ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV,
            .Descriptor =
                D3D12_ROOT_DESCRIPTOR1{
                    .ShaderRegister = 0u,
                    .RegisterSpace = 0u,
                    .Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE,
                },
            .ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX,
        },
    };

    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc = {
        .Version = D3D_ROOT_SIGNATURE_VERSION_1_1,
        .Desc_1_1 =
            {
                .NumParameters = 1u,
                .pParameters = root_parameters,
                .NumStaticSamplers = 0u,
                .pStaticSamplers = nullptr,
                .Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT,
            },
    };
    // Serialize root signature.
    throw_if_failed(D3D12SerializeVersionedRootSignature(&root_signature_desc, &root_signature_blob, nullptr));
    throw_if_failed(renderer.device->CreateRootSignature(0u, root_signature_blob->GetBufferPointer(),
                                                         root_signature_blob->GetBufferSize(),
                                                         IID_PPV_ARGS(&root_signature)));

    // Compile the vertex and pixel shader.
    Microsoft::WRL::ComPtr<ID3DBlob> shader_error_blob{};

    Microsoft::WRL::ComPtr<ID3DBlob> vertex_shader_blob{};
    throw_if_failed(D3DCompileFromFile(L"shaders/shader.hlsl", nullptr, nullptr, "vs_main", "vs_5_0", 0u, 0u,
                                       &vertex_shader_blob, &shader_error_blob));
    if (shader_error_blob)
    {
        printf("Shader compiler error (vertex) : %s.\n", (const char *)(shader_error_blob->GetBufferPointer()));
    }

    Microsoft::WRL::ComPtr<ID3DBlob> pixel_shader_blob{};
    throw_if_failed(D3DCompileFromFile(L"shaders/shader.hlsl", nullptr, nullptr, "ps_main", "ps_5_0", 0u, 0u,
                                       &pixel_shader_blob, nullptr));
    if (shader_error_blob)
    {
        printf("Shader compiler error (pixel) : %s.\n", (const char *)(shader_error_blob->GetBufferPointer()));
    }

    // Create the root signature.
    const D3D12_INPUT_ELEMENT_DESC input_element_descs[2] = {
        D3D12_INPUT_ELEMENT_DESC{
            .SemanticName = "POSITION",
            .SemanticIndex = 0u,
            .Format = DXGI_FORMAT_R32G32B32_FLOAT,
            .InputSlot = 0u,
            .AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT,
            .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            .InstanceDataStepRate = 0u,
        },
        D3D12_INPUT_ELEMENT_DESC{
            .SemanticName = "COLOR",
            .SemanticIndex = 0u,
            .Format = DXGI_FORMAT_R32G32B32_FLOAT,
            .InputSlot = 0u,
            .AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT,
            .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            .InstanceDataStepRate = 0u,
        },
    };

    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso{};
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC graphics_pso_desc = {
        .pRootSignature = root_signature.Get(),
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
                .FillMode = D3D12_FILL_MODE_SOLID,
                .CullMode = D3D12_CULL_MODE_BACK,
                .FrontCounterClockwise = FALSE,
            },
        .DepthStencilState =
            {
                .DepthEnable = TRUE,
                .DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL,
                .DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL,
                .StencilEnable = FALSE,
            },
        .InputLayout =
            {
                .pInputElementDescs = input_element_descs,
                .NumElements = 2u,
            },
        .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
        .NumRenderTargets = 1u,
        .RTVFormats =
            {
                DXGI_FORMAT_R10G10B10A2_UNORM,
            },
        .DSVFormat = DXGI_FORMAT_D32_FLOAT,
        .SampleDesc =
            {
                1u,
                0u,
            },
        .NodeMask = 0u,
    };
    throw_if_failed(renderer.device->CreateGraphicsPipelineState(&graphics_pso_desc, IID_PPV_ARGS(&pso)));

    // Create viewport and scissor.
    const D3D12_VIEWPORT viewport = {
        .TopLeftX = 0.0f,
        .TopLeftY = 0.0f,
        .Width = (float)window.m_width,
        .Height = (float)window.m_height,
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

    // Flush the gpu.
    renderer.execute_command_list();
    renderer.flush_gpu();

    intermediate_resources.clear();

    u64 frame_count = 0u;

    bool quit = false;

    // Timing related data.

    // Get the performance counter frequency (in seconds).
    LARGE_INTEGER performance_frequency = {};
    QueryPerformanceFrequency(&performance_frequency);

    const float seconds_per_count = 1.0f / (float)performance_frequency.QuadPart;

    LARGE_INTEGER frame_start_time = {};
    LARGE_INTEGER frame_end_time = {};

    float delta_time = 0.0;

    Camera camera{};
    camera.camera_rotation_speed = 1.0f;

    while (!quit)
    {
        QueryPerformanceCounter(&frame_start_time);

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

        const float aspect_ratio = (float)window.m_width / (float)window.m_height;
        const float vertical_fov = DirectX::XMConvertToRadians(45.0f);
        const DirectX::XMMATRIX projection_matrix =
            DirectX::XMMatrixPerspectiveFovLH(vertical_fov, aspect_ratio, 0.1, 100.0f);

        const DirectX::XMMATRIX view_projection_matrix =
            camera.update_and_get_view_matrix(delta_time) * projection_matrix;

        // Update cbuffers.
        SceneConstantBuffer buffer = {
            .view_projection_matrix = view_projection_matrix,
        };
        memcpy(scene_constant_buffer.buffer_ptr, &buffer, sizeof(SceneConstantBuffer));

        // Main render loop.

        // First, reset the command allocator and command list.
        u8 &swapchain_backbuffer_index = renderer.swapchain_backbuffer_index;
        ID3D12GraphicsCommandList *command_list = renderer.command_list.Get();

        throw_if_failed(renderer.direct_command_allocators[swapchain_backbuffer_index]->Reset());
        throw_if_failed(
            command_list->Reset(renderer.direct_command_allocators[swapchain_backbuffer_index].Get(), nullptr));

        // Get the backbuffer rtv cpu descriptor handle, transition the back buffer to render target, and clear rt.
        D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle =
            renderer.swapchain_backbuffer_cpu_descriptor_handles[swapchain_backbuffer_index];

        ID3D12Resource *const current_backbuffer_resource =
            renderer.swapchain_backbuffer_resources[swapchain_backbuffer_index].Get();

        // Transition the backbuffer from presentation mode to render target mode.
        const D3D12_RESOURCE_BARRIER presentation_to_render_target_barrier = {
            .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
            .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
            .Transition =
                {
                    .pResource = current_backbuffer_resource,
                    .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                    .StateBefore = D3D12_RESOURCE_STATE_PRESENT,
                    .StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET,
                },
        };

        command_list->ResourceBarrier(1u, &presentation_to_render_target_barrier);

        // Now, clear the RTV and DSV.
        const float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        command_list->ClearRenderTargetView(rtv_handle, clear_color, 0u, nullptr);
        command_list->ClearDepthStencilView(dsv_cpu_handle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0u, 0u, nullptr);

        // Set viewport.
        command_list->RSSetViewports(1u, &viewport);
        command_list->RSSetScissorRects(1u, &scissor_rect);

        // Set the index buffer, pso and all config settings for rendering.
        command_list->SetGraphicsRootSignature(root_signature.Get());
        command_list->SetPipelineState(pso.Get());

        ID3D12DescriptorHeap *const shader_visible_descriptor_heaps = {
            renderer.cbv_srv_uav_descriptor_heap.descriptor_heap.Get()};
        command_list->SetDescriptorHeaps(1u, &shader_visible_descriptor_heaps);

        command_list->OMSetRenderTargets(
            1u, &renderer.swapchain_backbuffer_cpu_descriptor_handles[swapchain_backbuffer_index], FALSE,
            &dsv_cpu_handle);

        command_list->SetGraphicsRootConstantBufferView(0u, scene_constant_buffer.buffer->GetGPUVirtualAddress());

        command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        command_list->IASetVertexBuffers(0u, 1u, &vertex_buffer_view);
        command_list->DrawInstanced(chunk_vertex_data.size(), 1u, 0u, 0u);

        // Now, transition back to presentation mode.
        const D3D12_RESOURCE_BARRIER render_target_to_presentation_barrier = {
            .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
            .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
            .Transition =
                {
                    .pResource = current_backbuffer_resource,
                    .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                    .StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET,
                    .StateAfter = D3D12_RESOURCE_STATE_PRESENT,
                },
        };

        command_list->ResourceBarrier(1u, &render_target_to_presentation_barrier);

        // Submit command list to queue for execution.
        renderer.execute_command_list();

        // Now, present the rendertarget and signal command queue.
        throw_if_failed(renderer.swapchain->Present(1u, 0u));
        renderer.signal_fence();

        renderer.swapchain_backbuffer_index = renderer.swapchain->GetCurrentBackBufferIndex();

        // Wait for the previous frame (that is presenting to swpachain_backbuffer_index) to complete execution.
        renderer.wait_for_fence_value_at_index(renderer.swapchain_backbuffer_index);

        ++frame_count;

        QueryPerformanceCounter(&frame_end_time);

        delta_time = (frame_end_time.QuadPart - frame_start_time.QuadPart) * seconds_per_count;
    }

    printf("Frames renderer :: %u", (u32)frame_count);

    renderer.flush_gpu();

    return 0;
}