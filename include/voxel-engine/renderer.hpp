#pragma once

enum class BufferTypes : u8
{
    StructuredBuffer,
    ConstantBuffer,
};

// Note : For simplicity purposes, the renderer will have a vector of buffers.
// The user will only have a index into this buffer.
struct Buffer
{
    u64 resource_index{};

    u64 cbv_index{};
    u64 srv_index{};
    u64 uav_index{};
    u64 rtv_index{};

    u8 *resource_ptr{};
};

// A simple descriptor heap abstraction.
// Provides simple methods to offset current descriptor to make creation of resources easier.
struct DescriptorHeap
{
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptor_heap{};

    D3D12_CPU_DESCRIPTOR_HANDLE current_cpu_descriptor_handle{};
    D3D12_GPU_DESCRIPTOR_HANDLE current_gpu_descriptor_handle{};

    u64 current_descriptor_handle_index{};

    u64 descriptor_handle_size{};

    D3D12_GPU_DESCRIPTOR_HANDLE get_gpu_descriptor_handle_at_index(const u64 index) const;
    D3D12_CPU_DESCRIPTOR_HANDLE get_cpu_descriptor_handle_at_index(const u64 index) const;

    void offset_current_descriptor_handles();

    void create(ID3D12Device *const device, const u32 num_descriptors,
                const D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type,
                const D3D12_DESCRIPTOR_HEAP_FLAGS descriptor_heap_flags);
};

// A simple & straight forward high level renderer abstraction.
struct Renderer
{
    explicit Renderer(const HWND window_handle, const u16 window_width, const u16 window_height);

    // Resource creation functions.
    Buffer create_buffer(const void *data, const size_t buffer_size, const BufferTypes buffer_type);

    void execute_command_list();

    // Sync functions.
    void wait_for_fence_value_at_index(const u32 frame_fence_values_index);

    // Whenever fence is being signalled, increment the monotonical frame fence value.
    // and update frame fence value.
    void signal_fence();

    void flush_gpu();

  private:
    // This function automatically offset's the current descriptor handle of descriptor heap.
    D3D12_CPU_DESCRIPTOR_HANDLE create_constant_buffer_view(const u64 buffer_resource_index, const size_t size);

  public:
    // Static globals.
    static inline constexpr u8 NUMBER_OF_BACKBUFFERS = 2u;

  private:
    // Core D3D12 and DXGI objects.
    Microsoft::WRL::ComPtr<ID3D12Debug> m_debug_device{};
    Microsoft::WRL::ComPtr<IDXGIFactory6> m_dxgi_factory{};
    Microsoft::WRL::ComPtr<IDXGIAdapter4> m_dxgi_adapter{};

    Microsoft::WRL::ComPtr<ID3D12Device2> m_device{};

    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_direct_command_queue{};
    Microsoft::WRL::ComPtr<IDXGISwapChain4> m_swapchain{};

    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, NUMBER_OF_BACKBUFFERS> m_swapchain_backbuffer_resources{};
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, NUMBER_OF_BACKBUFFERS> m_swapchain_backbuffer_cpu_descriptor_handles{};

    DescriptorHeap m_cbv_srv_uav_descriptor_heap{};
    DescriptorHeap m_rtv_descriptor_heap{};
    DescriptorHeap m_dsv_descriptor_heap{};

    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_direct_command_allocators[NUMBER_OF_BACKBUFFERS];
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_command_list{};

    Microsoft::WRL::ComPtr<ID3D12Fence> m_fence{};
    u64 m_monotonic_fence_value{};
    std::array<u64, NUMBER_OF_BACKBUFFERS> m_frame_fence_values{};

    u8 m_swapchain_backbuffer_index{};

    // Resource vectors.
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> m_resources{};
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> m_intermediate_resources{};
};
