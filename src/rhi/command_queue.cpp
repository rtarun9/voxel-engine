#include "voxel-engine/rhi/command_queue.hpp"

namespace rhi
{
void direct_command_queue_t::create(ID3D12Device *const device)
{
    const D3D12_COMMAND_QUEUE_DESC command_queue_desc = {
        .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
        .Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
        .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
        .NodeMask = 0u,
    };
    throw_if_failed(device->CreateCommandQueue(&command_queue_desc, IID_PPV_ARGS(&m_command_queue)));
    name_d3d12_object(m_command_queue.Get(), L"Direct command queue");

    // Create the command allocator (the underlying allocation where gpu commands will be stored after being
    // recorded by command list). Each frame has its own command allocator.
    for (u8 i = 0; i < NUMBER_OF_BACKBUFFERS; i++)
    {
        throw_if_failed(
            device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_command_allocators[i])));
        name_d3d12_object(m_command_queue.Get(), L"Direct queue command allocator " + std::to_wstring(i));
    }

    // Create the graphics command list.
    throw_if_failed(device->CreateCommandList(0u, D3D12_COMMAND_LIST_TYPE_DIRECT, m_command_allocators[0].Get(),
                                              nullptr, IID_PPV_ARGS(&m_command_list)));
    name_d3d12_object(m_command_queue.Get(), L"Direct queue command list");

    // Create a fence for CPU GPU synchronization.
    throw_if_failed(device->CreateFence(0u, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
    name_d3d12_object(m_fence.Get(), L"Direct queue fence");
}

void direct_command_queue_t::reset(const u8 index) const
{
    // Reset command allocator and command list.
    throw_if_failed(m_command_allocators[index]->Reset());
    throw_if_failed(m_command_list->Reset(m_command_allocators[index].Get(), nullptr));
}

void direct_command_queue_t::execute_command_list() const
{
    throw_if_failed(m_command_list->Close());

    ID3D12CommandList *const command_lists_to_execute[1] = {m_command_list.Get()};

    m_command_queue->ExecuteCommandLists(1u, command_lists_to_execute);
}

void direct_command_queue_t::wait_for_fence_value_at_index(const u8 index)
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

void direct_command_queue_t::signal_fence(const u8 index)
{
    throw_if_failed(m_command_queue->Signal(m_fence.Get(), ++m_monotonic_fence_value));
    m_frame_fence_values[index] = m_monotonic_fence_value;
}

void direct_command_queue_t::flush_queue()
{
    signal_fence(0);

    for (u32 i = 0; i < NUMBER_OF_BACKBUFFERS; i++)
    {
        m_frame_fence_values[i] = m_monotonic_fence_value;
    }

    wait_for_fence_value_at_index(0);
}

void copy_command_queue_t::create(ID3D12Device *const device)
{
    const D3D12_COMMAND_QUEUE_DESC command_queue_desc = {
        .Type = D3D12_COMMAND_LIST_TYPE_COPY,
        .Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
        .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
        .NodeMask = 0u,
    };
    throw_if_failed(device->CreateCommandQueue(&command_queue_desc, IID_PPV_ARGS(&m_command_queue)));
    name_d3d12_object(m_command_queue.Get(), L"Copy command queue");

    // Create a fence for CPU GPU synchronization.
    throw_if_failed(device->CreateFence(0u, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
    name_d3d12_object(m_fence.Get(), L"Copy command queue fence");
}

copy_command_queue_t::command_allocator_list_pair_t copy_command_queue_t::get_command_allocator_list_pair(
    ID3D12Device *const device)
{
    if (!m_command_allocator_list_queue.empty() &&
        m_command_allocator_list_queue.front().m_fence_value <= m_fence->GetCompletedValue())
    {
        const command_allocator_list_pair_t front = m_command_allocator_list_queue.front();
        m_command_allocator_list_queue.pop();

        // Reset list and allocator.
        throw_if_failed(front.m_command_allocator->Reset());
        throw_if_failed(front.m_command_list->Reset(front.m_command_allocator.Get(), nullptr));

        return front;
    }
    else
    {
        command_allocator_list_pair_t command_allocator_list_pair{};
        throw_if_failed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY,
                                                       IID_PPV_ARGS(&command_allocator_list_pair.m_command_allocator)));
        name_d3d12_object(command_allocator_list_pair.m_command_allocator.Get(), L"Copy command queue allocator");

        throw_if_failed(device->CreateCommandList(0u, D3D12_COMMAND_LIST_TYPE_COPY,
                                                  command_allocator_list_pair.m_command_allocator.Get(), nullptr,
                                                  IID_PPV_ARGS(&command_allocator_list_pair.m_command_list)));
        name_d3d12_object(command_allocator_list_pair.m_command_list.Get(), L"Copy command queue command list");

        return command_allocator_list_pair;
    }
}

void copy_command_queue_t::execute_command_list(command_allocator_list_pair_t &&alloc_list_pair)
{
    throw_if_failed(alloc_list_pair.m_command_list->Close());

    ID3D12CommandList *const command_lists_to_execute[1] = {alloc_list_pair.m_command_list.Get()};

    m_command_queue->ExecuteCommandLists(1u, command_lists_to_execute);

    throw_if_failed(m_command_queue->Signal(m_fence.Get(), ++m_monotonic_fence_value));

    alloc_list_pair.m_fence_value = m_monotonic_fence_value;

    m_command_allocator_list_queue.push(std::move(alloc_list_pair));
}

void copy_command_queue_t::flush_queue()
{
    throw_if_failed(m_command_queue->Signal(m_fence.Get(), ++m_monotonic_fence_value));
    throw_if_failed(m_fence->SetEventOnCompletion(m_monotonic_fence_value, nullptr));
}

} // namespace rhi
