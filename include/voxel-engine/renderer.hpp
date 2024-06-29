#pragma once

// Note : For simplicity purposes, the renderer will have a vector of ID3D12Resources.
// The user will only have a index into this vector to access the resource.
struct StructuredBuffer
{
    size_t resource_index{};
    size_t srv_index{};
};

struct ConstantBuffer
{
    size_t resource_index{};
    size_t cbv_index{};
    size_t size_in_bytes{};

    u8 *resource_mapped_ptr{};

    inline void update(const void *data) const
    {
        memcpy(resource_mapped_ptr, data, size_in_bytes);
    }
};

// The command buffer is a bit different. It internally has two resources, a default and upload heap.
// the update function is not similar to constant buffer, as here data is copied from the upload to default resource.
struct CommandBuffer
{
    size_t default_resource_index{};
    size_t upload_resource_index{};

    u8 *upload_resource_mapped_ptr{};

    // note(rtarun9) : Interesting chose to pass the resource into this functionn, but only for POC :(
    void update(ID3D12GraphicsCommandList *const command_list, const void *data, ID3D12Resource *const default_resource,
                ID3D12Resource *const upload_resource, const size_t size) const;
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
    StructuredBuffer create_structured_buffer(const void *data, const size_t stride, const size_t num_elements,
                                              const std::wstring_view buffer_name);
    ConstantBuffer create_constant_buffer(const size_t size_in_bytes, const std::wstring_view buffer_name);
    CommandBuffer create_command_buffer(const size_t size_in_bytes, const std::wstring_view buffer_name);

  private:
    // This function automatically offset's the current descriptor handle of descriptor heap.
    size_t create_constant_buffer_view(const size_t buffer_resource_index, const size_t size);
    size_t create_shader_resource_view(const size_t buffer_resource_index, const size_t stride,
                                       const size_t num_elements);

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

    // Resource vectors.
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> m_resources{};

    // note(rtarun9) : Try to figure out when is a good time to get rid of the intermediate resources.
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> m_intermediate_resources{};

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
