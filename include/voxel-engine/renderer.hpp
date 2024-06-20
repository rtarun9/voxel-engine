#pragma once

// Note : For simplicity purposes, the renderer will have a vector of ID3D12Resources.
// The user will only have a index into this vector to access the resource.
struct StructuredBuffer
{
    size_t resource_index{};
    size_t srv_index{};
};

struct IndexBuffer
{
    size_t resource_index{};
    size_t indices_count{};
    D3D12_INDEX_BUFFER_VIEW index_buffer_view{};
};

struct ConstantBuffer
{
    size_t resource_index{};
    size_t cbv_index{};
    size_t size_in_bytes{};

    u8 *resource_mapped_ptr{};

    void update(const void *data) const
    {
        memcpy(resource_mapped_ptr, data, size_in_bytes);
    }
};

// A simple & straight forward high level renderer abstraction.
struct Renderer
{
    // Nested struct definitions.
  private:
    // A simple descriptor heap abstraction.
    // Provides simple methods to offset current descriptor to make creation of resources easier.
    struct DescriptorHeap
    {
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptor_heap{};

        D3D12_CPU_DESCRIPTOR_HANDLE current_cpu_descriptor_handle{};
        D3D12_GPU_DESCRIPTOR_HANDLE current_gpu_descriptor_handle{};

        size_t current_descriptor_handle_index{};

        size_t descriptor_handle_size{};

        D3D12_GPU_DESCRIPTOR_HANDLE get_gpu_descriptor_handle_at_index(const size_t index) const;
        D3D12_CPU_DESCRIPTOR_HANDLE get_cpu_descriptor_handle_at_index(const size_t index) const;

        void offset_current_descriptor_handles();

        void create(ID3D12Device *const device, const size_t num_descriptors,
                    const D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type,
                    const D3D12_DESCRIPTOR_HEAP_FLAGS descriptor_heap_flags);
    };

  public:
    explicit Renderer(const HWND window_handle, const u16 window_width, const u16 window_height);

    // Resource creation functions.
    IndexBuffer create_index_buffer(const void *data, const size_t stride, const size_t indices_count);
    StructuredBuffer create_structured_buffer(const void *data, const size_t stride, const size_t num_elements);
    ConstantBuffer create_constant_buffer(const size_t size_in_bytes);

    void execute_command_list() const;

    // Sync functions.
    void wait_for_fence_value_at_index(const size_t frame_fence_values_index);

    // Whenever fence is being signalled, increment the monotonical frame fence value.
    // and update frame fence value.
    void signal_fence();

    void flush_gpu();

  private:
    // This function automatically offset's the current descriptor handle of descriptor heap.
    size_t create_constant_buffer_view(const size_t buffer_resource_index, const size_t size);
    size_t create_shader_resource_view(const size_t buffer_resource_index, const size_t stride,
                                       const size_t num_elements);

  public:
    // Static globals.
    static inline constexpr u8 NUMBER_OF_BACKBUFFERS = 2u;
    static inline constexpr DXGI_FORMAT BACKBUFFER_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;

  public:
    // Core D3D12 and DXGI objects.
    Microsoft::WRL::ComPtr<ID3D12Debug> m_debug_device{};
    Microsoft::WRL::ComPtr<IDXGIFactory6> m_dxgi_factory{};
    Microsoft::WRL::ComPtr<IDXGIAdapter4> m_dxgi_adapter{};

    Microsoft::WRL::ComPtr<ID3D12Device2> m_device{};

    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_direct_command_queue{};
    Microsoft::WRL::ComPtr<IDXGISwapChain4> m_swapchain{};

    std::array<u64, NUMBER_OF_BACKBUFFERS> m_swapchain_backbuffer_resource_indices{};
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

    // Bindless root signature, that is shared by all pipelines.
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_bindless_root_signature{};

    // Mutex used for resource creation.
    std::mutex m_resource_mutex{};
};
