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
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapchain{};
    const DXGI_SWAP_CHAIN_DESC1 swapchain_desc = {
        .Width = window.m_width,
        .Height = window.m_height,
        .Format = DXGI_FORMAT_R10G10B10A2_UNORM,
        .Stereo = FALSE,
        .SampleDesc = {1, 0},
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = 2u,
        .Scaling = DXGI_SCALING_NONE,
        .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
        .AlphaMode = DXGI_ALPHA_MODE_IGNORE,
        .Flags = 0u,
    };
    throw_if_failed(dxgi_factory->CreateSwapChainForHwnd(direct_command_queue.Get(), window.m_handle, &swapchain_desc,
                                                         nullptr, nullptr, &swapchain));

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
        .NumDescriptors = 2u,
        .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
        .NodeMask = 0u,
    };
    throw_if_failed(device->CreateDescriptorHeap(&rtv_descriptor_heap_desc, IID_PPV_ARGS(&rtv_descriptor_heap)));

    // Create the render target view for the swapchain back buffer.
    Microsoft::WRL::ComPtr<ID3D12Resource> swapchain_backbuffer_resources[2] = {};
    D3D12_CPU_DESCRIPTOR_HANDLE swapchain_backbuffer_cpu_descriptor_handles[2] = {};

    const u32 rtv_descriptor_handle_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv_descriptor_handle_for_heap_start =
        rtv_descriptor_heap->GetCPUDescriptorHandleForHeapStart();

    for (u8 i = 0; i < 2u; i++)
    {
        throw_if_failed(swapchain->GetBuffer(i, IID_PPV_ARGS(&(swapchain_backbuffer_resources[i]))));
        swapchain_backbuffer_cpu_descriptor_handles[i] = rtv_descriptor_handle_for_heap_start;
        swapchain_backbuffer_cpu_descriptor_handles[i].ptr += (u64)i * rtv_descriptor_handle_size;

        device->CreateRenderTargetView(swapchain_backbuffer_resources[i].Get(), nullptr,
                                       swapchain_backbuffer_cpu_descriptor_handles[i]);
    }

    // Create the command allocator (the underlying allocation where gpu commands will be stored after being recorded by
    // command list).
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> direct_command_allocator{};
    throw_if_failed(
        device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&direct_command_allocator)));

    // Create the graphics command list.
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list{};
    throw_if_failed(device->CreateCommandList(0u, D3D12_COMMAND_LIST_TYPE_DIRECT, direct_command_allocator.Get(),
                                              nullptr, IID_PPV_ARGS(&command_list)));

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
    }

    return 0;
}