#include "voxel-engine/rhi/descriptor_heap.hpp"

namespace rhi
{

void descriptor_heap_t::offset_current_descriptor_handles()
{
    current_cpu_descriptor_handle.ptr += descriptor_handle_size;
    current_gpu_descriptor_handle.ptr += descriptor_handle_size;

    current_descriptor_handle_index++;
}

void descriptor_heap_t::create(ID3D12Device *const device, const u32 num_descriptors,
                               const D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type,
                               const D3D12_DESCRIPTOR_HEAP_FLAGS descriptor_heap_flags)
{
    const D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_desc = {
        .Type = descriptor_heap_type,
        .NumDescriptors = num_descriptors,
        .Flags = descriptor_heap_flags,
        .NodeMask = 0u,
    };

    throw_if_failed(device->CreateDescriptorHeap(&descriptor_heap_desc, IID_PPV_ARGS(&descriptor_heap)));

    current_cpu_descriptor_handle = descriptor_heap->GetCPUDescriptorHandleForHeapStart();

    if (descriptor_heap_flags == D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
    {
        current_gpu_descriptor_handle = descriptor_heap->GetGPUDescriptorHandleForHeapStart();
    }

    current_descriptor_handle_index = 0u;

    descriptor_handle_size = device->GetDescriptorHandleIncrementSize(descriptor_heap_type);
}

} // namespace rhi