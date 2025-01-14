#pragma once

#include "common.hpp"

namespace rhi
{
// Abstraction over copy and direct command queue.

// Command queue abstraction that holds the queue, allocators, command list and sync primitives.
// Each queue type has its own struct since they operate in different ways (copy queue is async and requires thread
// sync primitives, while the direct queue is not for now).
struct direct_command_queue_t
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

struct copy_command_queue_t
{
    struct command_allocator_list_pair_t
    {
        ComPtr<ID3D12CommandAllocator> m_command_allocator{};
        ComPtr<ID3D12GraphicsCommandList> m_command_list{};
        u64 m_fence_value{};
    };

    std::queue<command_allocator_list_pair_t> m_command_allocator_list_queue{};
    ComPtr<ID3D12CommandQueue> m_command_queue{};

    ComPtr<ID3D12Fence> m_fence{};
    u64 m_monotonic_fence_value{};

    void create(ID3D12Device *const device);

    // If there is a allocator / list pair that has completed execution, return it. Else, create a new one.
    command_allocator_list_pair_t get_command_allocator_list_pair(ID3D12Device *const device);

    // Execute command list and move the allocator list pair back to the queue.
    void execute_command_list(command_allocator_list_pair_t &&alloc_list_pair);

    void flush_queue();
};

} // namespace rhi