#include "pch.hpp"

#include "types.hpp"
#include "window.hpp"

#include "common.hpp"

// Note that buffer_ptr is to only be used by constant buffers, intermediate_buffer is
struct Buffer
{
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer{};
    u8 *buffer_ptr{};
};

// Static buffer : contents set once cannot be reset, the resource is in fast GPU only accesible memory.
// Dynamic buffer (a hlsl constant buffer) : Placed in memory accesible by both CPU and GPU.
enum class BufferTypes : u8
{
    Static,
    Dynamic,
};

// Note that since intermediate buffer resources need to be in memory until the command list recorded operations are
// executed, they are stored in a temporary vector that is cleared when intermediate buffers are no longer required.
static std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> intermediate_buffers{};

// Helper function to create buffer. If buffer contains data, the upload operation (and GPU flush) must be done after
// function invocation .

Buffer create_buffer(ID3D12Device *const device, ID3D12GraphicsCommandList *const command_list, const void *data,
                     const u32 buffer_size, const BufferTypes buffer_type)
{
    Buffer buffer{};

    // First, create a upload buffer (that is placed in memory accesible by both GPU and CPU).
    // If the buffer type is constant buffer, this IS the final buffer.
    const D3D12_HEAP_PROPERTIES upload_heap_properties = {
        .Type = D3D12_HEAP_TYPE_UPLOAD,
        .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
        .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
        .CreationNodeMask = 0u,
        .VisibleNodeMask = 0u,
    };

    const D3D12_RESOURCE_DESC buffer_resource_desc = {
        .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Width = buffer_size,
        .Height = 1u,
        .DepthOrArraySize = 1u,
        .MipLevels = 1u,
        .Format = DXGI_FORMAT_UNKNOWN,
        .SampleDesc = {1u, 0u},
        .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
        .Flags = D3D12_RESOURCE_FLAG_NONE,
    };

    Microsoft::WRL::ComPtr<ID3D12Resource> intermediate_buffer{};
    throw_if_failed(device->CreateCommittedResource(
        &upload_heap_properties, D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES, &buffer_resource_desc,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&intermediate_buffer)));

    // Now that a resource is created, copy CPU data to this upload buffer.
    const D3D12_RANGE read_range{.Begin = 0u, .End = 0u};

    throw_if_failed(intermediate_buffer->Map(0u, &read_range, (void **)&buffer.buffer_ptr));

    if (data != nullptr)
    {
        memcpy(buffer.buffer_ptr, data, buffer_size);
    }

    if (buffer_type == BufferTypes::Dynamic)
    {
        buffer.buffer = intermediate_buffer;
        return buffer;
    }

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
        &default_heap_properties, D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES, &buffer_resource_desc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&buffer.buffer)));

    command_list->CopyResource(buffer.buffer.Get(), intermediate_buffer.Get());

    intermediate_buffers.emplace_back(intermediate_buffer);

    return buffer;
}

// A simple descriptor heap abstraction.
// Provides simple methods to offset current descriptor to make creation of resources easier.
// note(rtarun9) : Should the descriptor handle for heap start be stored in the struct?
struct DescriptorHeap
{
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptor_heap{};
    D3D12_CPU_DESCRIPTOR_HANDLE current_cpu_descriptor_handle{};
    D3D12_GPU_DESCRIPTOR_HANDLE current_gpu_descriptor_handle{};
    u32 descriptor_handle_size{};

    D3D12_GPU_DESCRIPTOR_HANDLE get_gpu_descriptor_handle_at_index(const u32 index)
    {
        D3D12_GPU_DESCRIPTOR_HANDLE handle = descriptor_heap->GetGPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<u64>(index) * descriptor_handle_size;

        return handle;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE get_cpu_descriptor_handle_at_index(const u32 index)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = descriptor_heap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<u64>(index) * descriptor_handle_size;

        return handle;
    }

    void offset()
    {
        current_cpu_descriptor_handle.ptr += descriptor_handle_size;
        current_gpu_descriptor_handle.ptr += descriptor_handle_size;
    }

    DescriptorHeap(ID3D12Device *const device, const u32 num_descriptors,
                   const D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type,
                   const D3D12_DESCRIPTOR_HEAP_FLAGS descriptor_heap_flags)
    {
        const D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_desc = {
            .Type = descriptor_heap_type,
            .NumDescriptors = num_descriptors,
            .Flags = descriptor_heap_flags,
            .NodeMask = 0u,

        };

        throw_if_failed(device->CreateDescriptorHeap(&descriptor_heap_desc, IID_PPV_ARGS(&descriptor_heap)));

        current_cpu_descriptor_handle = descriptor_heap->GetCPUDescriptorHandleForHeapStart();

        if (descriptor_heap_flags == D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
        {
            current_gpu_descriptor_handle = descriptor_heap->GetGPUDescriptorHandleForHeapStart();
        }

        descriptor_handle_size = device->GetDescriptorHandleIncrementSize(descriptor_heap_type);
    }
};

int main()
{
    intermediate_buffers.reserve(50);

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
    DescriptorHeap cbv_srv_uav_descriptor_heap = DescriptorHeap(
        device.Get(), 10u, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);

    DescriptorHeap rtv_descriptor_heap =
        DescriptorHeap(device.Get(), 10u, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);

    // Create the render target view for the swapchain back buffer.
    Microsoft::WRL::ComPtr<ID3D12Resource> swapchain_backbuffer_resources[number_of_backbuffers] = {};
    D3D12_CPU_DESCRIPTOR_HANDLE swapchain_backbuffer_cpu_descriptor_handles[number_of_backbuffers] = {};

    for (u8 i = 0; i < number_of_backbuffers; i++)
    {
        throw_if_failed(swapchain->GetBuffer(i, IID_PPV_ARGS(&(swapchain_backbuffer_resources[i]))));
        swapchain_backbuffer_cpu_descriptor_handles[i] = rtv_descriptor_heap.get_cpu_descriptor_handle_at_index(i);

        device->CreateRenderTargetView(swapchain_backbuffer_resources[i].Get(), nullptr,
                                       swapchain_backbuffer_cpu_descriptor_handles[i]);
    }

    // Create the command allocator (the underlying allocation where gpu commands will be stored after being
    // recorded by command list). Each frame has its own command allocator.
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
    Buffer index_buffer = create_buffer(device.Get(), command_list.Get(), (void *)&index_buffer_data, sizeof(u16) * 3u,
                                        BufferTypes::Static);
    // Create the index buffer view.
    const D3D12_INDEX_BUFFER_VIEW index_buffer_view = {

        .BufferLocation = index_buffer.buffer->GetGPUVirtualAddress(),
        .SizeInBytes = sizeof(u16) * 3u,
        .Format = DXGI_FORMAT_R16_UINT,
    };

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

    Buffer vertex_buffer = create_buffer(device.Get(), command_list.Get(), (void *)&vertex_buffer_data,
                                         sizeof(VertexBuffer) * 3u, BufferTypes::Static);
    D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view{};

    // Create the vertex buffer view.
    vertex_buffer_view = {
        .BufferLocation = vertex_buffer.buffer->GetGPUVirtualAddress(),
        .SizeInBytes = sizeof(VertexBuffer) * 3u,
        .StrideInBytes = sizeof(VertexBuffer),
    };

    // Create a constant buffer for simple linear algebra tests.
    struct alignas(256) ConstantBuffer
    {
        DirectX::XMMATRIX matrix{};
    };

    Buffer constant_buffer =
        create_buffer(device.Get(), command_list.Get(), nullptr, sizeof(ConstantBuffer), BufferTypes::Dynamic);
    // Create the constant buffer descriptor.
    const D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {
        .BufferLocation = constant_buffer.buffer->GetGPUVirtualAddress(),
        .SizeInBytes = sizeof(ConstantBuffer),
    };

    // Assuming that currently only 1 cbv srv uav descriptor is present.
    D3D12_CPU_DESCRIPTOR_HANDLE constant_buffer_descriptor_handle =
        cbv_srv_uav_descriptor_heap.get_cpu_descriptor_handle_at_index(0u);

    device->CreateConstantBufferView(&cbv_desc, constant_buffer_descriptor_handle);

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

    intermediate_buffers.clear();

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
        memcpy(constant_buffer.buffer_ptr, &buffer, sizeof(ConstantBuffer));

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

        ID3D12DescriptorHeap *const shader_visible_descriptor_heaps = {
            cbv_srv_uav_descriptor_heap.descriptor_heap.Get()};
        command_list->SetDescriptorHeaps(1u, &shader_visible_descriptor_heaps);

        command_list->OMSetRenderTargets(1u, &swapchain_backbuffer_cpu_descriptor_handles[swapchain_backbuffer_index],
                                         FALSE, nullptr);
        command_list->SetGraphicsRootConstantBufferView(0u, constant_buffer.buffer->GetGPUVirtualAddress());
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