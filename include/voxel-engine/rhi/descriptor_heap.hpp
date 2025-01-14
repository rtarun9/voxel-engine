#pragma once

namespace rhi
{

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
} // namespace rhi