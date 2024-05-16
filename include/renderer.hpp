#pragma once

#include "common.hpp"
#include "types.hpp"

// Static buffer : contents set once cannot be updated, the resource is in fast GPU only accesible memory.
// Dynamic buffer (a constant buffer) : Placed in memory accesible by both CPU and GPU.
enum class BufferTypes : u8
{
    Static,
    Dynamic,
};

struct Buffer
{
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer{};
    u8 *buffer_ptr{};
};

// A simple descriptor heap abstraction.
// Provides simple methods to offset current descriptor to make creation of resources easier.
// note(rtarun9) : Should the descriptor handle for heap start be stored in the struct?
struct DescriptorHeap
{
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptor_heap{};
    D3D12_CPU_DESCRIPTOR_HANDLE current_cpu_descriptor_handle{};
    D3D12_GPU_DESCRIPTOR_HANDLE current_gpu_descriptor_handle{};
    u32 descriptor_handle_size{};

    D3D12_GPU_DESCRIPTOR_HANDLE get_gpu_descriptor_handle_at_index(const u32 index) const;
    D3D12_CPU_DESCRIPTOR_HANDLE get_cpu_descriptor_handle_at_index(const u32 index) const;

    void offset()
    {
        current_cpu_descriptor_handle.ptr += descriptor_handle_size;
        current_gpu_descriptor_handle.ptr += descriptor_handle_size;
    }

    void create(ID3D12Device *const device, const u32 num_descriptors,
                const D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type,
                const D3D12_DESCRIPTOR_HEAP_FLAGS descriptor_heap_flags);
};

// A simple & straight forward high level renderer abstraction.
struct Renderer
{
    Renderer(const HWND window_handle, const u16 window_width, const u16 window_height);

    // Sync functions.
    void wait_for_fence_value_at_index(const u32 frame_fence_values_index);

    // Whenever fence is being signalled, increment the monotonical frame fence value.
    // and update frame fence value.
    void signal_fence();

    void flush_gpu();

    // Create a buffer, and return the intermediate buffer and the GPU-only buffer.
    // If buffer type is dynamic, the intermediate buffer can be ignored.
    struct BufferPair
    {
        Buffer intermediate_buffer;
        Buffer buffer;
    };

    // Resource creation functions.
    BufferPair create_buffer(const void *data, const u32 buffer_size, const BufferTypes buffer_type);

    // This function automatically offset's the current descriptor handle of descriptor heap.
    D3D12_CPU_DESCRIPTOR_HANDLE create_constant_buffer_view(const Buffer &buffer, const u32 size);

    // Executes command list.
    void execute_command_list();

    // Static globals.
    static inline constexpr u8 NUMBER_OF_BACKBUFFERS = 2u;

    // Core D3D12 and DXGI objects.
    Microsoft::WRL::ComPtr<ID3D12Debug> debug_device{};
    Microsoft::WRL::ComPtr<IDXGIFactory6> dxgi_factory{};
    Microsoft::WRL::ComPtr<IDXGIAdapter4> dxgi_adapter{};

    Microsoft::WRL::ComPtr<ID3D12Device2> device{};

    Microsoft::WRL::ComPtr<ID3D12CommandQueue> direct_command_queue{};
    Microsoft::WRL::ComPtr<IDXGISwapChain4> swapchain{};

    Microsoft::WRL::ComPtr<ID3D12Resource> swapchain_backbuffer_resources[NUMBER_OF_BACKBUFFERS];
    D3D12_CPU_DESCRIPTOR_HANDLE swapchain_backbuffer_cpu_descriptor_handles[NUMBER_OF_BACKBUFFERS];

    DescriptorHeap cbv_srv_uav_descriptor_heap{};
    DescriptorHeap rtv_descriptor_heap{};
    DescriptorHeap dsv_descriptor_heap{};

    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> direct_command_allocators[NUMBER_OF_BACKBUFFERS];
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list{};

    Microsoft::WRL::ComPtr<ID3D12Fence> fence{};
    u64 monotonic_fence_value{};
    u64 frame_fence_values[NUMBER_OF_BACKBUFFERS];

    u8 swapchain_backbuffer_index{};
};