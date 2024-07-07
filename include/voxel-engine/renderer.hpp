#pragma once

struct StructuredBuffer
{
    Microsoft::WRL::ComPtr<ID3D12Resource> resource{};
    size_t srv_index{};
};

struct ConstantBuffer
{
    Microsoft::WRL::ComPtr<ID3D12Resource> resource{};
    size_t cbv_index{};
    size_t size_in_bytes{};

    u8 *resource_mapped_ptr{};

    inline void update(const void *data) const
    {
        memcpy(resource_mapped_ptr, data, size_in_bytes);
    }
};

struct IndexBuffer
{
    Microsoft::WRL::ComPtr<ID3D12Resource> resource{};
    size_t indices_count{};
    D3D12_INDEX_BUFFER_VIEW index_buffer_view{};
};

// The command buffer is a bit different. It internally has two resources, a default and upload heap.
// the update function is not similar to constant buffer, as here data is copied from the upload to default resource.
// The command buffer contains its ID3D12Resource directly since the same command buffer is used for the entire engine.
struct CommandBuffer
{
    Microsoft::WRL::ComPtr<ID3D12Resource> default_resource{};
    Microsoft::WRL::ComPtr<ID3D12Resource> upload_resource{};
    Microsoft::WRL::ComPtr<ID3D12Resource> zeroed_counter_buffer_resource{};

    u8 *upload_resource_mapped_ptr{};
    size_t upload_resource_srv_index{};
    size_t default_resource_uav_index{};
    size_t counter_offset{};
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
    // The functions return a buffer and intermediate resource (which can be discarded once the CopyResource operation
    // is complete).
    struct IndexBufferWithIntermediateResource
    {
        IndexBuffer index_buffer;
        Microsoft::WRL::ComPtr<ID3D12Resource> intermediate_resource;
    };

    IndexBufferWithIntermediateResource create_index_buffer(const void *data, const size_t stride,
                                                            const size_t indices_count,
                                                            const std::wstring_view buffer_name);

    struct StucturedBufferWithIntermediateResource
    {
        StructuredBuffer structured_buffer;
        Microsoft::WRL::ComPtr<ID3D12Resource> intermediate_resource;
    };

    StucturedBufferWithIntermediateResource create_structured_buffer(const void *data, const size_t stride,
                                                                     const size_t num_elements,
                                                                     const std::wstring_view buffer_name);

    CommandBuffer create_command_buffer(const size_t stride, const size_t max_number_of_elements,
                                        const std::wstring_view buffer_name);

    ConstantBuffer internal_create_constant_buffer(const size_t size_in_bytes, const std::wstring_view buffer_name);

    template <size_t T>
    std::array<ConstantBuffer, T> create_constant_buffer(const size_t size_in_bytes,
                                                         const std::wstring_view buffer_name);

  private:
    // This function automatically offset's the current descriptor handle of descriptor heap.
    size_t create_constant_buffer_view(ID3D12Resource *const resource, size_t size);
    size_t create_shader_resource_view(ID3D12Resource *const resource, const size_t stride, const size_t num_elements);
    size_t create_unordered_access_view(ID3D12Resource *const resource, const size_t stride, const size_t num_elements,
                                        const bool use_counter = false,
                                        const size_t counter_offset = D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT);

  public:
    // Static globals.
    static inline constexpr u8 NUMBER_OF_BACKBUFFERS = 3u;
    static inline constexpr DXGI_FORMAT BACKBUFFER_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;

    static inline constexpr u8 COPY_QUEUE_RING_BUFFER_SIZE = 10u;

  public:
    // Core D3D12 and DXGI objects.
    Microsoft::WRL::ComPtr<ID3D12Debug> m_debug_device{};
    Microsoft::WRL::ComPtr<IDXGIFactory6> m_dxgi_factory{};
    Microsoft::WRL::ComPtr<IDXGIAdapter4> m_dxgi_adapter{};

    Microsoft::WRL::ComPtr<ID3D12Device2> m_device{};

    Microsoft::WRL::ComPtr<IDXGISwapChain4> m_swapchain{};

    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, NUMBER_OF_BACKBUFFERS> m_swapchain_backbuffer_cpu_descriptor_handles{};
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, NUMBER_OF_BACKBUFFERS> m_swapchain_backbuffer_resources{};

    DescriptorHeap m_cbv_srv_uav_descriptor_heap{};
    DescriptorHeap m_rtv_descriptor_heap{};
    DescriptorHeap m_dsv_descriptor_heap{};

    u8 m_swapchain_backbuffer_index{};

    // Bindless root signature, that is shared by all pipelines.
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_bindless_root_signature{};

    // Mutex used for resource creation.
    std::mutex m_resource_mutex{};

    // Command queue abstraction that holds the queue, allocators, command list and sync primitives.
    // Each queue type has its own struct since they operate in different ways (copy queue is async and requires thread
    // sync primitives, while the direct queue is not for now).
    struct DirectCommandQueue
    {
        std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, NUMBER_OF_BACKBUFFERS> m_command_allocators{};
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_command_queue{};
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_command_list{};

        Microsoft::WRL::ComPtr<ID3D12Fence> m_fence{};
        u64 m_monotonic_fence_value{};
        std::array<u64, NUMBER_OF_BACKBUFFERS> m_frame_fence_values{};

        void create(ID3D12Device *const device);

        void reset(const u8 index) const;
        void execute_command_list() const;
        void wait_for_fence_value_at_index(const u8 index);
        void signal_fence(const u8 index);

        void flush_queue();
    };

    struct CopyCommandQueue
    {
        struct CommandAllocatorListPair
        {
            Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_command_allocator{};
            Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_command_list{};
            u64 m_fence_value{};
        };

        std::queue<CommandAllocatorListPair> m_command_allocator_list_queue{};
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_command_queue{};

        Microsoft::WRL::ComPtr<ID3D12Fence> m_fence{};
        u64 m_monotonic_fence_value{};

        void create(ID3D12Device *const device);

        // If there is a allocator / list pair that has completed execution, return it. Else, create a new one.
        CommandAllocatorListPair get_command_allocator_list_pair(ID3D12Device *const device);

        // Execute command list and move the allocator list pair back to the queue.
        void execute_command_list(CommandAllocatorListPair &&alloc_list_pair);

        void flush_queue();
    };

    DirectCommandQueue m_direct_queue{};
    CopyCommandQueue m_copy_queue{};
};

template <size_t T>
inline std::array<ConstantBuffer, T> Renderer::create_constant_buffer(const size_t size_in_bytes,
                                                                      const std::wstring_view buffer_name)
{
    std::array<ConstantBuffer, T> constant_buffers{};
    for (size_t i = 0; i < T; i++)
    {
        constant_buffers[i] =
            internal_create_constant_buffer(size_in_bytes, std::wstring(buffer_name) + std::to_wstring(i));
    }

    return constant_buffers;
}
