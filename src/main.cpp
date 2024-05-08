#include "pch.hpp"

#include "types.hpp"
#include "window.hpp"

#include "common.hpp"

int main()
{
    const Window window(1080u, 720u);

    // Enable the debug layer in debug mode.
    Microsoft::WRL::ComPtr<ID3D12Debug> debug{};

    if constexpr (VX_DEBUG_MODE)
    {
        throw_if_failed(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));
        debug->EnableDebugLayer();
    }

    // Create the DXGI Factory so we get access to DXGI objects (like adapters).
    u32 dxgi_factory_creation_flags = 0u;

    if constexpr (VX_DEBUG_MODE)
    {
        dxgi_factory_creation_flags = DXGI_CREATE_FACTORY_DEBUG;
    }

    Microsoft::WRL::ComPtr<IDXGIFactory6> dxgi_factory{};
    throw_if_failed(CreateDXGIFactory2(dxgi_factory_creation_flags, IID_PPV_ARGS(&dxgi_factory)));

    // Get the adapter with best performance. Print the selected adapter's details to console.
    Microsoft::WRL::ComPtr<IDXGIAdapter4> dxgi_adapter{};
    throw_if_failed(dxgi_factory->EnumAdapterByGpuPreference(0u, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                             IID_PPV_ARGS(&dxgi_adapter)));

    DXGI_ADAPTER_DESC adapter_desc{};
    throw_if_failed(dxgi_adapter->GetDesc(&adapter_desc));
    printf("Selected adapter desc :: %ls.\n", adapter_desc.Description);

    // Create the d3d12 device (logical adapter : All d3d objects require d3d12 device for creation).
    Microsoft::WRL::ComPtr<ID3D12Device2> device{};
    throw_if_failed(D3D12CreateDevice(dxgi_adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)));

    // In debug mode, setup the info queue so breakpoint is placed whenever a error / warning occurs that is d3d
    // related.
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> info_queue{};
    if constexpr (VX_DEBUG_MODE)
    {
        throw_if_failed(device.As(&info_queue));

        throw_if_failed(info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE));
        throw_if_failed(info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE));
        throw_if_failed(info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE));
    }

    // Setup the direct command queue.
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> direct_command_queue{};
    const D3D12_COMMAND_QUEUE_DESC direct_command_queue_desc = {
        .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
        .Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
        .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
        .NodeMask = 0u,
    };
    throw_if_failed(device->CreateCommandQueue(&direct_command_queue_desc, IID_PPV_ARGS(&direct_command_queue)));

    // Create the dxgi swapchain.
    constexpr u8 number_of_backbuffers = 2u;

    Microsoft::WRL::ComPtr<IDXGISwapChain3> swapchain{};
    {
        Microsoft::WRL::ComPtr<IDXGISwapChain1> swapchain_1{};
        const DXGI_SWAP_CHAIN_DESC1 swapchain_desc = {
            .Width = window.m_width,
            .Height = window.m_height,
            .Format = DXGI_FORMAT_R10G10B10A2_UNORM,
            .Stereo = FALSE,
            .SampleDesc = {1, 0},
            .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
            .BufferCount = number_of_backbuffers,
            .Scaling = DXGI_SCALING_NONE,
            .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
            .AlphaMode = DXGI_ALPHA_MODE_IGNORE,
            .Flags = 0u,
        };
        throw_if_failed(dxgi_factory->CreateSwapChainForHwnd(direct_command_queue.Get(), window.m_handle,
                                                             &swapchain_desc, nullptr, nullptr, &swapchain_1));

        throw_if_failed(swapchain_1.As(&swapchain));
    }

    // Create a cbv srv uav descriptor heap.
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> cbv_srv_uav_descriptor_heap{};
    const D3D12_DESCRIPTOR_HEAP_DESC cbv_srv_uav_descriptor_heap_desc = {
        .Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        .NumDescriptors = 10u,
        .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
        .NodeMask = 0u,
    };
    throw_if_failed(
        device->CreateDescriptorHeap(&cbv_srv_uav_descriptor_heap_desc, IID_PPV_ARGS(&cbv_srv_uav_descriptor_heap)));

    // Create a rtv descriptor heap.
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_descriptor_heap{};
    const D3D12_DESCRIPTOR_HEAP_DESC rtv_descriptor_heap_desc = {
        .Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
        .NumDescriptors = number_of_backbuffers,
        .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
        .NodeMask = 0u,
    };
    throw_if_failed(device->CreateDescriptorHeap(&rtv_descriptor_heap_desc, IID_PPV_ARGS(&rtv_descriptor_heap)));

    // Create the render target view for the swapchain back buffer.
    Microsoft::WRL::ComPtr<ID3D12Resource> swapchain_backbuffer_resources[number_of_backbuffers] = {};
    D3D12_CPU_DESCRIPTOR_HANDLE swapchain_backbuffer_cpu_descriptor_handles[number_of_backbuffers] = {};

    const u32 rtv_descriptor_handle_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv_descriptor_handle_for_heap_start =
        rtv_descriptor_heap->GetCPUDescriptorHandleForHeapStart();

    for (u8 i = 0; i < number_of_backbuffers; i++)
    {
        throw_if_failed(swapchain->GetBuffer(i, IID_PPV_ARGS(&(swapchain_backbuffer_resources[i]))));
        swapchain_backbuffer_cpu_descriptor_handles[i] = rtv_descriptor_handle_for_heap_start;
        swapchain_backbuffer_cpu_descriptor_handles[i].ptr += (u64)i * rtv_descriptor_handle_size;

        device->CreateRenderTargetView(swapchain_backbuffer_resources[i].Get(), nullptr,
                                       swapchain_backbuffer_cpu_descriptor_handles[i]);
    }

    // Create the command allocator (the underlying allocation where gpu commands will be stored after being recorded by
    // command list).
    // Each frame has its own command allocator.
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> direct_command_allocators[number_of_backbuffers] = {};
    for (u8 i = 0; i < number_of_backbuffers; i++)
    {
        throw_if_failed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                       IID_PPV_ARGS(&direct_command_allocators[i])));
    }

    // Create the graphics command list.
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list{};
    throw_if_failed(device->CreateCommandList(0u, D3D12_COMMAND_LIST_TYPE_DIRECT, direct_command_allocators[0].Get(),
                                              nullptr, IID_PPV_ARGS(&command_list)));

    // Create a fence for CPU GPU synchronization.
    Microsoft::WRL::ComPtr<ID3D12Fence> fence{};
    throw_if_failed(device->CreateFence(0u, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));

    // The monotonically increasing fence value.
    u64 monotonic_fence_value = 0u;

    // The per frame fence values (used to determine if rendering of previous frame is completed).
    u64 frame_fence_values[2] = {};

    u8 swapchain_backbuffer_index = swapchain->GetCurrentBackBufferIndex();

    // Helper functions related to GPU - CPU synchronization.

    // Wait for GPU to reach a specified value.
    const auto wait_for_fence_value = [&](const u64 fence_value_to_wait_for) {
        if (fence->GetCompletedValue() >= fence_value_to_wait_for)
        {
            return;
        }
        else
        {
            throw_if_failed(fence->SetEventOnCompletion(fence_value_to_wait_for, nullptr));
        }
    };

    // Signal the command queue with a specified value.
    const auto signal_fence = [&](const u64 fence_value_to_signal) {
        throw_if_failed(direct_command_queue->Signal(fence.Get(), fence_value_to_signal));
    };

    // Create the resources required for rendering.
    // Index buffer setup.
    constexpr u16 index_buffer_data[3] = {0u, 1u, 2u};
    Microsoft::WRL::ComPtr<ID3D12Resource> upload_resource{};
    Microsoft::WRL::ComPtr<ID3D12Resource> index_buffer_resource{};
    D3D12_INDEX_BUFFER_VIEW index_buffer_view{};
    {
        // To upload the index buffer data to GPU - only memory, we need to first create a staging buffer in CPU - GPU
        // accessible memory, then copy the data from CPU to this intermediate memory, then to GPU only memory.

        // Create the staging commited resource.
        // A commited resource creates both a resource and heap large enough to fit the resource.

        // The heap type is : Upload (because CPU has access and it is optimized for uploading to GPU).
        const D3D12_HEAP_PROPERTIES upload_heap_properties = {
            .Type = D3D12_HEAP_TYPE_UPLOAD,
            .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
            .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
            .CreationNodeMask = 0u,
            .VisibleNodeMask = 0u,
        };

        constexpr u32 index_buffer_size = sizeof(u32) * 3u;

        const D3D12_RESOURCE_DESC index_buffer_resource_desc = {
            .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
            .Width = index_buffer_size,
            .Height = 1u,
            .DepthOrArraySize = 1u,
            .MipLevels = 1u,
            .Format = DXGI_FORMAT_UNKNOWN,
            .SampleDesc = {1u, 0u},
            .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
            .Flags = D3D12_RESOURCE_FLAG_NONE,
        };

        throw_if_failed(device->CreateCommittedResource(
            &upload_heap_properties, D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES, &index_buffer_resource_desc,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&upload_resource)));

        // Now that a resource is created, copy CPU data to this upload buffer.
        u8 *upload_buffer_pointer = nullptr;
        const D3D12_RANGE read_range{.Begin = 0u, .End = 0u};

        throw_if_failed(upload_resource->Map(0u, &read_range, (void **)&upload_buffer_pointer));
        memcpy(upload_buffer_pointer, index_buffer_data, index_buffer_size);

        // Create the final resource and transfer the data from upload buffer to the final buffer.
        // The heap type is : Default (no CPU access).
        const D3D12_HEAP_PROPERTIES default_heap_properties = {
            .Type = D3D12_HEAP_TYPE_DEFAULT,
            .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
            .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
            .CreationNodeMask = 0u,
            .VisibleNodeMask = 0u,
        };

        throw_if_failed(device->CreateCommittedResource(
            &default_heap_properties, D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES, &index_buffer_resource_desc,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&index_buffer_resource)));

        command_list->CopyResource(index_buffer_resource.Get(), upload_resource.Get());

        // Create the index buffer view.
        index_buffer_view = {
            .BufferLocation = index_buffer_resource->GetGPUVirtualAddress(),
            .SizeInBytes = index_buffer_size,
            .Format = DXGI_FORMAT_R16_UINT,
        };
    }

    // Vertex buffer setup.
    struct VertexBuffer
    {
        DirectX::XMFLOAT3 position{};
        DirectX::XMFLOAT3 color{};
    };
    constexpr VertexBuffer vertex_buffer_data[3] = {
        VertexBuffer{.position = {-0.5f, -0.5f, 0.0f}, .color = {1.0f, 0.0f, 0.0f}},
        VertexBuffer{.position = {0.0f, 0.5f, 0.0f}, .color = {0.0f, 1.0f, 0.0f}},
        VertexBuffer{.position = {+0.5f, -0.5f, 0.0f}, .color = {0.0f, 0.0f, 1.0f}},
    };

    Microsoft::WRL::ComPtr<ID3D12Resource> vertex_buffer_upload_resource{};
    Microsoft::WRL::ComPtr<ID3D12Resource> vertex_buffer_resource{};
    D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view{};
    {
        const D3D12_HEAP_PROPERTIES upload_heap_properties = {
            .Type = D3D12_HEAP_TYPE_UPLOAD,
            .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
            .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
            .CreationNodeMask = 0u,
            .VisibleNodeMask = 0u,
        };

        constexpr u32 vertex_buffer_size = sizeof(VertexBuffer) * 3u;

        const D3D12_RESOURCE_DESC vertex_buffer_resource_desc = {
            .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
            .Width = vertex_buffer_size,
            .Height = 1u,
            .DepthOrArraySize = 1u,
            .MipLevels = 1u,
            .Format = DXGI_FORMAT_UNKNOWN,
            .SampleDesc = {1u, 0u},
            .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
            .Flags = D3D12_RESOURCE_FLAG_NONE,
        };

        throw_if_failed(device->CreateCommittedResource(
            &upload_heap_properties, D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES, &vertex_buffer_resource_desc,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&vertex_buffer_upload_resource)));

        // Now that a resource is created, copy CPU data to this upload buffer.
        u8 *upload_buffer_pointer = nullptr;
        const D3D12_RANGE read_range{.Begin = 0u, .End = 0u};

        throw_if_failed(vertex_buffer_upload_resource->Map(0u, &read_range, (void **)&upload_buffer_pointer));
        memcpy(upload_buffer_pointer, vertex_buffer_data, vertex_buffer_size);

        // Create the final resource and transfer the data from upload buffer to the final buffer.
        // The heap type is : Default (no CPU access).
        const D3D12_HEAP_PROPERTIES default_heap_properties = {
            .Type = D3D12_HEAP_TYPE_DEFAULT,
            .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
            .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
            .CreationNodeMask = 0u,
            .VisibleNodeMask = 0u,
        };

        throw_if_failed(device->CreateCommittedResource(
            &default_heap_properties, D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES, &vertex_buffer_resource_desc,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&vertex_buffer_resource)));

        command_list->CopyResource(vertex_buffer_resource.Get(), vertex_buffer_upload_resource.Get());

        // Create the vertex buffer view.
        vertex_buffer_view = {
            .BufferLocation = vertex_buffer_resource->GetGPUVirtualAddress(),
            .SizeInBytes = vertex_buffer_size,
            .StrideInBytes = sizeof(VertexBuffer),
        };
    }

    // Create a constant buffer for simple linear algebra tests.
    struct alignas(256) ConstantBuffer
    {
        DirectX::XMMATRIX matrix{};
    };

    Microsoft::WRL::ComPtr<ID3D12Resource> constant_buffer{};

    u8 *constant_buffer_ptr = nullptr;
    {
        const D3D12_HEAP_PROPERTIES upload_heap_properties = {
            .Type = D3D12_HEAP_TYPE_UPLOAD,
            .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
            .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
            .CreationNodeMask = 0u,
            .VisibleNodeMask = 0u,
        };

        constexpr u32 constant_buffer_size = sizeof(ConstantBuffer);

        const D3D12_RESOURCE_DESC constant_buffer_resource_desc = {
            .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
            .Width = constant_buffer_size,
            .Height = 1u,
            .DepthOrArraySize = 1u,
            .MipLevels = 1u,
            .Format = DXGI_FORMAT_UNKNOWN,
            .SampleDesc = {1u, 0u},
            .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
            .Flags = D3D12_RESOURCE_FLAG_NONE,
        };

        throw_if_failed(device->CreateCommittedResource(
            &upload_heap_properties, D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES, &constant_buffer_resource_desc,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&constant_buffer)));
        constant_buffer->SetName(L"Constant Buffer Resource");

        const D3D12_RANGE read_range{.Begin = 0u, .End = 0u};

        throw_if_failed(constant_buffer->Map(0u, &read_range, (void **)&constant_buffer_ptr));

        // Create the constant buffer descriptor.
        const D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {
            .BufferLocation = constant_buffer->GetGPUVirtualAddress(),
            .SizeInBytes = sizeof(ConstantBuffer),
        };

        D3D12_CPU_DESCRIPTOR_HANDLE constant_buffer_descriptor_handle =
            cbv_srv_uav_descriptor_heap->GetCPUDescriptorHandleForHeapStart();
        constant_buffer_descriptor_handle.ptr +=
            device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) * number_of_backbuffers;

        device->CreateConstantBufferView(&cbv_desc, constant_buffer_descriptor_handle);
    }

    // Create a empty root signature.
    Microsoft::WRL::ComPtr<ID3DBlob> root_signature_blob{};
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature{};

    D3D12_ROOT_PARAMETER1 root_parameter = {
        .ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV,
        .Descriptor =
            D3D12_ROOT_DESCRIPTOR1{
                .ShaderRegister = 0u,
                .RegisterSpace = 0u,
                .Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE,
            },
        .ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX,
    };

    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc = {
        .Version = D3D_ROOT_SIGNATURE_VERSION_1_1,
        .Desc_1_1 =
            {
                .NumParameters = 1u,
                .pParameters = &root_parameter,
                .NumStaticSamplers = 0u,
                .pStaticSamplers = nullptr,
                .Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT,
            },
    };
    // Serialize root signature.
    throw_if_failed(D3D12SerializeVersionedRootSignature(&root_signature_desc, &root_signature_blob, nullptr));
    throw_if_failed(device->CreateRootSignature(0u, root_signature_blob->GetBufferPointer(),
                                                root_signature_blob->GetBufferSize(), IID_PPV_ARGS(&root_signature)));

    // Compile the vertex and pixel shader.
    Microsoft::WRL::ComPtr<ID3DBlob> shader_error_blob{};

    Microsoft::WRL::ComPtr<ID3DBlob> vertex_shader_blob{};
    throw_if_failed(D3DCompileFromFile(L"shaders/triangle_shader.hlsl", nullptr, nullptr, "vs_main", "vs_5_0", 0u, 0u,
                                       &vertex_shader_blob, &shader_error_blob));
    if (shader_error_blob)
    {
        printf("Shader compiler error (vertex) : %s.\n", (const char *)(shader_error_blob->GetBufferPointer()));
    }

    Microsoft::WRL::ComPtr<ID3DBlob> pixel_shader_blob{};
    throw_if_failed(D3DCompileFromFile(L"shaders/triangle_shader.hlsl", nullptr, nullptr, "ps_main", "ps_5_0", 0u, 0u,
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
                .DepthEnable = FALSE,
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
        .SampleDesc =
            {
                1u,
                0u,
            },
        .NodeMask = 0u,
    };
    throw_if_failed(device->CreateGraphicsPipelineState(&graphics_pso_desc, IID_PPV_ARGS(&pso)));

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
    throw_if_failed(command_list->Close());

    ID3D12CommandList *const command_lists_to_execute[1] = {command_list.Get()};

    direct_command_queue->ExecuteCommandLists(1u, command_lists_to_execute);
    ++monotonic_fence_value;
    for (auto &frame_fence_value : frame_fence_values)
    {
        frame_fence_value = monotonic_fence_value;
    }
    signal_fence(monotonic_fence_value);
    wait_for_fence_value(frame_fence_values[swapchain_backbuffer_index]);

    u64 frame_count = 0u;

    bool quit = false;
    while (!quit)
    {
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

        // Update cbuffers.
        ConstantBuffer buffer = {
            .matrix = DirectX::XMMatrixRotationZ(frame_count / 100.0f),
        };
        memcpy(constant_buffer_ptr, &buffer, sizeof(ConstantBuffer));

        // Main render loop.

        // First, reset the command allocator and command list.
        throw_if_failed(direct_command_allocators[swapchain_backbuffer_index]->Reset());
        throw_if_failed(command_list->Reset(direct_command_allocators[swapchain_backbuffer_index].Get(), nullptr));

        // Get the backbuffer rtv cpu descriptor handle, transition the back buffer to render target, and clear rt.
        D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle =
            swapchain_backbuffer_cpu_descriptor_handles[swapchain_backbuffer_index];

        ID3D12Resource *current_backbuffer_resource = swapchain_backbuffer_resources[swapchain_backbuffer_index].Get();

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

        // Now, clear the RTV.
        const float clear_color[4] = {cosf(frame_count / 100.0f), sinf(frame_count / 100.0f), 0.0f, 1.0f};
        command_list->ClearRenderTargetView(rtv_handle, clear_color, 0u, nullptr);

        // Set viewport.
        command_list->RSSetViewports(1u, &viewport);
        command_list->RSSetScissorRects(1u, &scissor_rect);

        // Set the index buffer, pso and all config settings for rendering.
        command_list->SetGraphicsRootSignature(root_signature.Get());
        command_list->SetPipelineState(pso.Get());

        ID3D12DescriptorHeap *const shader_visible_descriptor_heaps = {cbv_srv_uav_descriptor_heap.Get()};
        command_list->SetDescriptorHeaps(1u, &shader_visible_descriptor_heaps);

        command_list->OMSetRenderTargets(1u, &swapchain_backbuffer_cpu_descriptor_handles[swapchain_backbuffer_index],
                                         FALSE, nullptr);
        command_list->SetGraphicsRootConstantBufferView(0u, constant_buffer->GetGPUVirtualAddress());
        command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        command_list->IASetIndexBuffer(&index_buffer_view);
        command_list->IASetVertexBuffers(0u, 1u, &vertex_buffer_view);
        command_list->DrawInstanced(3u, 1u, 0u, 0u);

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
        throw_if_failed(command_list->Close());

        ID3D12CommandList *const command_lists_to_execute[1] = {command_list.Get()};

        direct_command_queue->ExecuteCommandLists(1u, command_lists_to_execute);

        // Now, present the rendertarget and signal command queue.
        throw_if_failed(swapchain->Present(1u, 0u));
        monotonic_fence_value++;
        frame_fence_values[swapchain_backbuffer_index] = monotonic_fence_value;

        signal_fence(monotonic_fence_value);

        swapchain_backbuffer_index = swapchain->GetCurrentBackBufferIndex();

        // Wait for the previous frame (that is presenting to swpachain_backbuffer_index) to complete execution.
        wait_for_fence_value(frame_fence_values[swapchain_backbuffer_index]);

        frame_count++;
    }

    wait_for_fence_value(monotonic_fence_value);

    return 0;
}