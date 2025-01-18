#pragma once

namespace rhi
{
struct swapchain_backbuffer_t
{
    D3D12_CPU_DESCRIPTOR_HANDLE m_rtv_cpu_descriptor_handle{};
    ComPtr<ID3D12Resource> m_resource{};
};

struct structured_buffer_t
{
    ComPtr<ID3D12Resource> m_resource{};
    u32 m_srv_index{};
};

template <typename T> struct constant_buffer_t
{
    T m_data{};
    ComPtr<ID3D12Resource> m_resource{};
    u32 m_cbv_index{};

    u8 *m_resource_mapped_ptr{};

    inline void update() const
    {
        memcpy(m_resource_mapped_ptr, (void *)&m_data, sizeof(T));
    }
};

struct index_buffer_t
{
    ComPtr<ID3D12Resource> m_resource{};
    u32 m_indices_count{};
    D3D12_INDEX_BUFFER_VIEW m_index_buffer_view{};
};

struct command_buffer_t
{
    ComPtr<ID3D12Resource> m_default_resource{};
    ComPtr<ID3D12Resource> m_upload_resource{};
    ComPtr<ID3D12Resource> m_zeroed_counter_buffer_resource{};

    u8 *m_upload_resource_mapped_ptr{};

    u32 m_upload_resource_srv_index{};
    u32 m_default_resource_uav_index{};
    size_t m_counter_offset{};
};
} // namespace rhi
