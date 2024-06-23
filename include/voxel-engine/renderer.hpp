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
    StructuredBuffer create_structured_buffer(const void *data, const size_t stride, const size_t num_elements);
    ConstantBuffer create_constant_buffer(const size_t size_in_bytes);

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
    // The template parameter is used to determine how many allocators are created.
    template <size_t T> struct CommandQueue
    {
        std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, T> m_command_allocators{};
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_command_queue{};
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_command_list{};

        Microsoft::WRL::ComPtr<ID3D12Fence> m_fence{};
        u64 m_monotonic_fence_value{};
        std::array<u64, T> m_frame_fence_values{};

        // NOTE : The below mutex should at ANY time be lockable by either a single worker thread or the main thread.
        mutable std::mutex m_queue_lock{};
        mutable std::condition_variable m_cv{};
        mutable bool m_is_command_list_closed{false};
        void create(ID3D12Device *const device, const D3D12_COMMAND_LIST_TYPE command_type)
        {

            const D3D12_COMMAND_QUEUE_DESC command_queue_desc = {
                .Type = command_type,
                .Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
                .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
                .NodeMask = 0u,
            };
            throw_if_failed(device->CreateCommandQueue(&command_queue_desc, IID_PPV_ARGS(&m_command_queue)));

            // Create the command allocator (the underlying allocation where gpu commands will be stored after being
            // recorded by command list). Each frame has its own command allocator.
            for (u8 i = 0; i < T; i++)
            {
                throw_if_failed(device->CreateCommandAllocator(command_type, IID_PPV_ARGS(&m_command_allocators[i])));
            }

            // Create the graphics command list.
            throw_if_failed(device->CreateCommandList(0u, command_type, m_command_allocators[0].Get(), nullptr,
                                                      IID_PPV_ARGS(&m_command_list)));

            // Create a fence for CPU GPU synchronization.
            throw_if_failed(device->CreateFence(0u, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
        }

        void reset(const u8 index) const
        {

            std::unique_lock<std::mutex> ul(m_queue_lock);

            const auto &allocator = m_command_allocators[index];
            const auto &command_list = m_command_list;

            // Reset command allocator and command list.
            throw_if_failed(allocator->Reset());
            throw_if_failed(command_list->Reset(allocator.Get(), nullptr));

            m_is_command_list_closed = false;
            m_cv.notify_one();
        }

        void execute_command_list() const
        {
            std::unique_lock<std::mutex> ul(m_queue_lock);
            m_is_command_list_closed = true;

            throw_if_failed(m_command_list->Close());

            ID3D12CommandList *const command_lists_to_execute[1] = {m_command_list.Get()};

            m_command_queue->ExecuteCommandLists(1u, command_lists_to_execute);
        }

        void wait_for_fence_value_at_index(const u8 index)
        {
            if (m_fence->GetCompletedValue() >= m_frame_fence_values[index])
            {
                return;
            }
            else
            {
                throw_if_failed(m_fence->SetEventOnCompletion(m_frame_fence_values[index], nullptr));
            }
        }

        void signal_fence(const u8 index)
        {
            throw_if_failed(m_command_queue->Signal(m_fence.Get(), ++m_monotonic_fence_value));
            m_frame_fence_values[index] = m_monotonic_fence_value;
        }

        void flush_queue()
        {

            signal_fence(0);

            for (u32 i = 0; i < T; i++)
            {
                m_frame_fence_values[i] = m_monotonic_fence_value;
            }

            wait_for_fence_value_at_index(0);
        }
    };

    CommandQueue<NUMBER_OF_BACKBUFFERS> m_direct_queue{};
    CommandQueue<COPY_QUEUE_RING_BUFFER_SIZE> m_copy_queue{};
};
