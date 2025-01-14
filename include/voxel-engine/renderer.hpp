#pragma once

struct structured_buffer_t
{
    ComPtr<ID3D12Resource> resource{};
    u32 srv_index{};
};

template <typename T> struct constant_buffer_t
{
    T data{};
    ComPtr<ID3D12Resource> resource{};
    u32 cbv_index{};

    u8 *resource_mapped_ptr{};

    inline void update() const
    {
        memcpy(resource_mapped_ptr, (void *)&data, sizeof(T));
    }
};

struct index_buffer_t
{
    ComPtr<ID3D12Resource> resource{};
    u32 indices_count{};
    D3D12_INDEX_BUFFER_VIEW index_buffer_view{};
};

// The command buffer is a bit different. It internally has two resources, a default and upload heap.
// the update function is not similar to constant buffer, as here data is copied from the upload to default resource.
// The command buffer contains its ID3D12Resource directly since the same command buffer is used for the entire engine.
struct command_buffer_t
{
    ComPtr<ID3D12Resource> default_resource{};
    ComPtr<ID3D12Resource> upload_resource{};
    ComPtr<ID3D12Resource> zeroed_counter_buffer_resource{};

    u8 *upload_resource_mapped_ptr{};

    u32 upload_resource_srv_index{};
    u32 default_resource_uav_index{};
    u32 counter_offset{};
};

// A simple & straight forward high level renderer abstraction.
struct renderer_t
{
    // Nested struct definitions.
  private:
    // A simple descriptor heap abstraction.
    // Provides simple methods to offset current descriptor to make creation of resources easier.
    struct descriptor_heap_t
    {
        ComPtr<ID3D12DescriptorHeap> descriptor_heap{};

        D3D12_CPU_DESCRIPTOR_HANDLE current_cpu_descriptor_handle{};
        D3D12_GPU_DESCRIPTOR_HANDLE current_gpu_descriptor_handle{};

        u32 current_descriptor_handle_index{};

        size_t descriptor_handle_size{};

        void offset_current_descriptor_handles();

        void create(ID3D12Device *const device, const u32 num_descriptors,
                    const D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type,
                    const D3D12_DESCRIPTOR_HEAP_FLAGS descriptor_heap_flags);
    };

  public:
    explicit renderer_t(const HWND window_handle, const u32 window_width, const u32 window_height);

    // Resource creation functions.
    // The functions return a buffer and intermediate resource (which can be discarded once the CopyResource operation
    // is complete).
    struct index_buffer_with_intermediate_resource_t
    {
        index_buffer_t index_buffer{};
        ComPtr<ID3D12Resource> intermediate_resource{};
    };

    index_buffer_with_intermediate_resource_t create_index_buffer(const void *data, const size_t stride,
                                                                  const u32 indices_count,
                                                                  const std::wstring_view buffer_name);

    struct structured_buffer_with_intermediate_resource_t
    {
        structured_buffer_t structured_buffer{};
        ComPtr<ID3D12Resource> intermediate_resource{};
    };

    structured_buffer_with_intermediate_resource_t create_structured_buffer(const void *data, const size_t stride,
                                                                            const u32 num_elements,
                                                                            const std::wstring_view buffer_name);

    command_buffer_t create_command_buffer(const size_t stride, const size_t max_number_of_elements,
                                           const std::wstring_view buffer_name);

    template <typename T> constant_buffer_t<T> create_constant_buffer(const std::wstring_view buffer_name);

    template <typename T, size_t N>
    std::array<constant_buffer_t<T>, N> create_constant_buffers(const std::wstring_view buffer_name);

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
    ComPtr<ID3D12Debug> m_debug_device{};
    ComPtr<IDXGIFactory6> m_dxgi_factory{};
    ComPtr<IDXGIAdapter4> m_dxgi_adapter{};

    ComPtr<ID3D12Device2> m_device{};

    ComPtr<IDXGISwapChain4> m_swapchain{};

    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, NUMBER_OF_BACKBUFFERS> m_swapchain_backbuffer_cpu_descriptor_handles{};
    std::array<ComPtr<ID3D12Resource>, NUMBER_OF_BACKBUFFERS> m_swapchain_backbuffer_resources{};

    descriptor_heap_t m_cbv_srv_uav_descriptor_heap{};
    descriptor_heap_t m_rtv_descriptor_heap{};
    descriptor_heap_t m_dsv_descriptor_heap{};

    u8 m_swapchain_backbuffer_index{};

    // Bindless root signature, that is shared by all pipelines.
    ComPtr<ID3D12RootSignature> m_bindless_root_signature{};

    // Mutex used for resource creation.
    std::mutex m_resource_mutex{};

    // Command queue abstraction that holds the queue, allocators, command list and sync primitives.
    // Each queue type has its own struct since they operate in different ways (copy queue is async and requires thread
    // sync primitives, while the direct queue is not for now).
    struct DirectCommandQueue
    {
        std::array<ComPtr<ID3D12CommandAllocator>, NUMBER_OF_BACKBUFFERS> m_command_allocators{};
        ComPtr<ID3D12CommandQueue> m_command_queue{};
        ComPtr<ID3D12GraphicsCommandList> m_command_list{};

        ComPtr<ID3D12Fence> m_fence{};
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
            ComPtr<ID3D12CommandAllocator> m_command_allocator{};
            ComPtr<ID3D12GraphicsCommandList> m_command_list{};
            u64 m_fence_value{};
        };

        std::queue<CommandAllocatorListPair> m_command_allocator_list_queue{};
        ComPtr<ID3D12CommandQueue> m_command_queue{};

        ComPtr<ID3D12Fence> m_fence{};
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

template <typename T>
inline constant_buffer_t<T> renderer_t::create_constant_buffer(const std::wstring_view buffer_name)
{
    constant_buffer_t<T> constant_buffer = {};

    const D3D12_HEAP_PROPERTIES upload_heap_properties = {
        .Type = D3D12_HEAP_TYPE_UPLOAD,
        .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
        .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
        .CreationNodeMask = 0u,
        .VisibleNodeMask = 0u,
    };

    const D3D12_RESOURCE_DESC buffer_resource_desc = {
        .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Alignment = 0u,
        .Width = sizeof(T),
        .Height = 1u,
        .DepthOrArraySize = 1u,
        .MipLevels = 1u,
        .Format = DXGI_FORMAT_UNKNOWN,
        .SampleDesc = {1u, 0u},
        .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
        .Flags = D3D12_RESOURCE_FLAG_NONE,
    };

    throw_if_failed(m_device->CreateCommittedResource(
        &upload_heap_properties, D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES | D3D12_HEAP_FLAG_CREATE_NOT_ZEROED,
        &buffer_resource_desc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr,
        IID_PPV_ARGS(&constant_buffer.resource)));

    // Now that a resource is created, copy CPU data to this upload buffer.
    const D3D12_RANGE read_range{.Begin = 0u, .End = 0u};

    throw_if_failed(constant_buffer.resource->Map(0u, &read_range, (void **)&constant_buffer.resource_mapped_ptr));

    std::scoped_lock<std::mutex> scoped_lock(m_resource_mutex);

    name_d3d12_object(constant_buffer.resource.Get(), buffer_name);

    // Create Constant buffer view.
    constant_buffer.cbv_index = create_constant_buffer_view(constant_buffer.resource.Get(), sizeof(T));

    return constant_buffer;
}

template <typename T, size_t N>
inline std::array<constant_buffer_t<T>, N> renderer_t::create_constant_buffers(const std::wstring_view buffer_name)
{
    std::array<constant_buffer_t<T>, N> constant_buffers{};
    for (size_t i = 0; i < N; i++)
    {
        constant_buffers[i] = create_constant_buffer<T>(std::wstring(buffer_name) + std::to_wstring(i));
    }

    return constant_buffers;
}
