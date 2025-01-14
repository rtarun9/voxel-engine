#pragma once

namespace rhi
{
struct swapchain_backbuffer_t
{
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_descriptor_handle{};
    ComPtr<ID3D12Resource> resource{};
};

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
    size_t counter_offset{};
};
} // namespace rhi
