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

    // Create the d3d12 device (logical adapter : All d3d device creation uses the d3d12 device).
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
    throw_if_failed(command_list->Close());

    // Create a fence for CPU GPU synchronization.
    Microsoft::WRL::ComPtr<ID3D12Fence> fence{};
    throw_if_failed(device->CreateFence(0u, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));

    // The monotonically increasing fence value.
    u64 monotonic_fence_value = 0u;

    // The per frame fence values (used to determine if rendering of previous frame is completed).
    u64 frame_fence_values[2] = {};

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
    constexpr u8 index_buffer_data[3] = {0u, 1u, 2u};
    Microsoft::WRL::ComPtr<ID3D12Resource> index_buffer_resource{};
    // D3D12_INDEX_BUFFER_VIEW index_buffer_view{};
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

        const D3D12_RESOURCE_DESC upload_resource_desc = {
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

        Microsoft::WRL::ComPtr<ID3D12Resource> upload_resource{};
        throw_if_failed(device->CreateCommittedResource(
            &upload_heap_properties, D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES, &upload_resource_desc,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&upload_resource)));

        // Now that a resource is created, copy CPU data to this upload buffer.
        u8 *upload_buffer_pointer = nullptr;
        const D3D12_RANGE read_range{.Begin = 0u, .End = 0u};

        throw_if_failed(upload_resource->Map(0u, &read_range, (void **)&upload_buffer_pointer));
        memcpy(upload_buffer_pointer, index_buffer_data, index_buffer_size);
    }

    u64 frame_count = 0u;
    u8 swapchain_backbuffer_index = swapchain->GetCurrentBackBufferIndex();

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