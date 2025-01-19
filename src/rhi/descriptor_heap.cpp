#include "voxel-engine/rhi/descriptor_heap.hpp"

namespace rhi
{

descriptor_handle_t descriptor_heap_t::get_then_offset_current_descriptor_handle()
{
    descriptor_handle_t result = m_current_descriptor_handle;

    m_current_descriptor_handle.m_cpu_descriptor_handle.ptr += m_descriptor_handle_size;
    m_current_descriptor_handle.m_gpu_descriptor_handle.ptr += m_descriptor_handle_size;

    m_current_descriptor_handle.m_index++;

    return result;
}

void descriptor_heap_t::create(ID3D12Device *const device, const u32 num_descriptors,
                               const D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type,
                               const D3D12_DESCRIPTOR_HEAP_FLAGS descriptor_heap_flags, const std::wstring_view name)
{
    assert(device);

    const D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_desc = {
        .Type = descriptor_heap_type,
        .NumDescriptors = num_descriptors,
        .Flags = descriptor_heap_flags,
        .NodeMask = 0u,
    };

    throw_if_failed(device->CreateDescriptorHeap(&descriptor_heap_desc, IID_PPV_ARGS(&m_descriptor_heap)));
    name_d3d12_object(m_descriptor_heap.Get(), name);

    m_current_descriptor_handle.m_cpu_descriptor_handle = m_descriptor_heap->GetCPUDescriptorHandleForHeapStart();

    if (descriptor_heap_flags == D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
    {
        m_current_descriptor_handle.m_gpu_descriptor_handle = m_descriptor_heap->GetGPUDescriptorHandleForHeapStart();
    }

    m_current_descriptor_handle.m_index = 0u;

    m_descriptor_handle_size = device->GetDescriptorHandleIncrementSize(descriptor_heap_type);
}

} // namespace rhi
