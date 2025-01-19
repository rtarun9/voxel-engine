#pragma once

namespace rhi
{

struct descriptor_handle_t
{
    D3D12_CPU_DESCRIPTOR_HANDLE m_cpu_descriptor_handle{};
    D3D12_GPU_DESCRIPTOR_HANDLE m_gpu_descriptor_handle{};

    u32 m_index{};
};

// A simple descriptor heap abstraction.
// Provides simple methods to offset current descriptor to make creation of resources easier.
struct descriptor_heap_t
{
    ComPtr<ID3D12DescriptorHeap> m_descriptor_heap{};
    descriptor_handle_t m_current_descriptor_handle{};

    size_t m_descriptor_handle_size{};

    descriptor_handle_t get_then_offset_current_descriptor_handle();

    void create(ID3D12Device *const device, const u32 num_descriptors,
                const D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type,
                const D3D12_DESCRIPTOR_HEAP_FLAGS descriptor_heap_flags, const std::wstring_view name);
};
} // namespace rhi