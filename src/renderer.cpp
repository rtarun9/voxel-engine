#include "pch.hpp"

#include "renderer.hpp"

D3D12_GPU_DESCRIPTOR_HANDLE DescriptorHeap::get_gpu_descriptor_handle_at_index(const u32 index) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE handle = descriptor_heap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<u64>(index) * descriptor_handle_size;

    return handle;
}
D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHeap::get_cpu_descriptor_handle_at_index(const u32 index) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = descriptor_heap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<u64>(index) * descriptor_handle_size;

    return handle;
}
void DescriptorHeap::create(ID3D12Device *const device, const u32 num_descriptors,
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

Renderer::Renderer(const HWND window_handle, const u16 window_width, const u16 window_height)
{
    // Enable the debug layer in debug mode.
    if constexpr (VX_DEBUG_MODE)
    {
        throw_if_failed(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_device)));
        debug_device->EnableDebugLayer();
    }

    // Create the DXGI Factory so we get access to DXGI objects (like adapters).
    u32 dxgi_factory_creation_flags = 0u;

    if constexpr (VX_DEBUG_MODE)
    {
        dxgi_factory_creation_flags = DXGI_CREATE_FACTORY_DEBUG;
    }

    throw_if_failed(CreateDXGIFactory2(dxgi_factory_creation_flags, IID_PPV_ARGS(&dxgi_factory)));

    // Get the adapter with best performance. Print the selected adapter's details to console.
    throw_if_failed(dxgi_factory->EnumAdapterByGpuPreference(0u, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                             IID_PPV_ARGS(&dxgi_adapter)));

    DXGI_ADAPTER_DESC adapter_desc{};
    throw_if_failed(dxgi_adapter->GetDesc(&adapter_desc));
    printf("Selected adapter desc :: %ls.\n", adapter_desc.Description);

    // Create the d3d12 device (logical adapter : All d3d objects require d3d12 device for creation).
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
    const D3D12_COMMAND_QUEUE_DESC direct_command_queue_desc = {
        .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
        .Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
        .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
        .NodeMask = 0u,
    };
    throw_if_failed(device->CreateCommandQueue(&direct_command_queue_desc, IID_PPV_ARGS(&direct_command_queue)));

    // Create the dxgi swapchain.
    {
        Microsoft::WRL::ComPtr<IDXGISwapChain1> swapchain_1{};
        const DXGI_SWAP_CHAIN_DESC1 swapchain_desc = {
            .Width = window_width,
            .Height = window_height,
            .Format = DXGI_FORMAT_R10G10B10A2_UNORM,
            .Stereo = FALSE,
            .SampleDesc = {1, 0},
            .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
            .BufferCount = NUMBER_OF_BACKBUFFERS,
            .Scaling = DXGI_SCALING_NONE,
            .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
            .AlphaMode = DXGI_ALPHA_MODE_IGNORE,
            .Flags = 0u,
        };
        throw_if_failed(dxgi_factory->CreateSwapChainForHwnd(direct_command_queue.Get(), window_handle, &swapchain_desc,
                                                             nullptr, nullptr, &swapchain_1));

        throw_if_failed(swapchain_1.As(&swapchain));
    }

    // Create descriptor heaps.
    cbv_srv_uav_descriptor_heap = DescriptorHeap{};
    cbv_srv_uav_descriptor_heap.create(device.Get(), 10u, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                       D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);

    rtv_descriptor_heap = DescriptorHeap{};
    rtv_descriptor_heap.create(device.Get(), 10u, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);

    dsv_descriptor_heap = DescriptorHeap{};
    dsv_descriptor_heap.create(device.Get(), 1u, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);

    // Create the render target view for the swapchain back buffer.
    for (u8 i = 0; i < NUMBER_OF_BACKBUFFERS; i++)
    {
        throw_if_failed(swapchain->GetBuffer(i, IID_PPV_ARGS(&(swapchain_backbuffer_resources[i]))));
        swapchain_backbuffer_cpu_descriptor_handles[i] = rtv_descriptor_heap.get_cpu_descriptor_handle_at_index(i);

        device->CreateRenderTargetView(swapchain_backbuffer_resources[i].Get(), nullptr,
                                       swapchain_backbuffer_cpu_descriptor_handles[i]);
    }

    // Create the command allocator (the underlying allocation where gpu commands will be stored after being
    // recorded by command list). Each frame has its own command allocator.
    for (u8 i = 0; i < NUMBER_OF_BACKBUFFERS; i++)
    {
        throw_if_failed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                       IID_PPV_ARGS(&direct_command_allocators[i])));
    }

    // Create the graphics command list.
    throw_if_failed(device->CreateCommandList(0u, D3D12_COMMAND_LIST_TYPE_DIRECT, direct_command_allocators[0].Get(),
                                              nullptr, IID_PPV_ARGS(&command_list)));

    // Create a fence for CPU GPU synchronization.
    throw_if_failed(device->CreateFence(0u, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));

    swapchain_backbuffer_index = swapchain->GetCurrentBackBufferIndex();
}

void Renderer::wait_for_fence_value_at_index(const u32 frame_fence_values_index)
{
    if (fence->GetCompletedValue() >= frame_fence_values[frame_fence_values_index])
    {
        return;
    }
    else
    {
        throw_if_failed(fence->SetEventOnCompletion(frame_fence_values[frame_fence_values_index], nullptr));
    }
}

// Whenever fence is being signalled, increment the monotonical frame fence value.
void Renderer::signal_fence()
{
    ++monotonic_fence_value;
    throw_if_failed(direct_command_queue->Signal(fence.Get(), monotonic_fence_value));
    frame_fence_values[swapchain_backbuffer_index] = monotonic_fence_value;
}

void Renderer::flush_gpu()
{
    signal_fence();
    for (u32 i = 0; i < Renderer::NUMBER_OF_BACKBUFFERS; i++)
    {
        frame_fence_values[i] = monotonic_fence_value;
    }

    wait_for_fence_value_at_index(swapchain_backbuffer_index);
}

Renderer::BufferPair Renderer::create_buffer(const void *data, const u32 buffer_size, const BufferTypes buffer_type)
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
        return BufferPair{.intermediate_buffer = {intermediate_buffer}, .buffer = buffer};
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

    return BufferPair{.intermediate_buffer = {intermediate_buffer}, .buffer = buffer};
}

D3D12_CPU_DESCRIPTOR_HANDLE Renderer::create_constant_buffer_view(const Buffer &buffer, const u32 size)
{
    const D3D12_CPU_DESCRIPTOR_HANDLE handle = cbv_srv_uav_descriptor_heap.current_cpu_descriptor_handle;

    const D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {
        .BufferLocation = buffer.buffer->GetGPUVirtualAddress(),
        .SizeInBytes = size,
    };

    device->CreateConstantBufferView(&cbv_desc, handle);

    cbv_srv_uav_descriptor_heap.offset();

    return handle;
}

void Renderer::execute_command_list()
{
    throw_if_failed(command_list->Close());

    ID3D12CommandList *const command_lists_to_execute[1] = {command_list.Get()};

    direct_command_queue->ExecuteCommandLists(1u, command_lists_to_execute);
}
