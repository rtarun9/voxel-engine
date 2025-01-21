#pragma once

#include "buffers.hpp"
#include "command_queue.hpp"
#include "common.hpp"
#include "descriptor_heap.hpp"

#include "tracy/Tracy.hpp"

namespace rhi
{
// A simple & straight forward high level renderer abstraction.
struct renderer_t
{
  public:
    explicit renderer_t(const HWND window_handle, const u32 window_width, const u32 window_height);

    // Resource creation functions.
    // The functions return a buffer and intermediate resource (which can be discarded once the CopyResource operation
    // is complete).
    // This is done because creation of index / other structured buffers requires a copy resource operation, which is
    // done on the async copy queue. Slightly inconvinient abstraction, but a required one :)
    struct index_buffer_with_intermediate_resource_t
    {
        index_buffer_t m_index_buffer{};
        ComPtr<ID3D12Resource> m_intermediate_resource{};
    };

    index_buffer_with_intermediate_resource_t create_index_buffer(const void *data, const size_t stride,
                                                                  const u32 indices_count,
                                                                  const std::wstring_view buffer_name);

    struct structured_buffer_with_intermediate_resource_t
    {
        structured_buffer_t m_structured_buffer{};
        ComPtr<ID3D12Resource> m_intermediate_resource{};
    };

    structured_buffer_with_intermediate_resource_t create_structured_buffer(const void *data, const size_t stride,
                                                                            const u32 num_elements,
                                                                            const std::wstring_view buffer_name);

    upload_structured_buffer_t create_upload_structured_buffer(const size_t stride, const u32 num_elements,
                                                               const std::wstring_view buffer_name);

    command_buffer_t create_command_buffer(const size_t stride, const size_t max_number_of_elements,
                                           const std::wstring_view buffer_name);

    template <typename T> constant_buffer_t<T> create_constant_buffer(const std::wstring_view buffer_name);

    template <typename T, size_t N>
    std::array<constant_buffer_t<T>, N> create_constant_buffers(const std::wstring_view buffer_name);

  private:
    // This function automatically offset's the current descriptor handle of descriptor heap.
    // NOTE: constant buffer creation function does not use a lock. Assumption is that multiple threads will NOT use /
    // update constant buffer.
    u32 create_constant_buffer_view(ID3D12Resource *const resource, size_t size);

    u32 create_shader_resource_view(ID3D12Resource *const resource, const size_t stride, const size_t num_elements);
    u32 create_unordered_access_view(ID3D12Resource *const resource, const size_t stride, const size_t num_elements,
                                     const bool use_counter = false,
                                     const size_t counter_offset = D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT);

  public:
    // Core D3D12 and DXGI objects.
    ComPtr<ID3D12Debug> m_debug_device{};
    ComPtr<IDXGIFactory6> m_dxgi_factory{};
    ComPtr<IDXGIAdapter4> m_dxgi_adapter{};

    ComPtr<ID3D12Device2> m_device{};

    ComPtr<IDXGISwapChain4> m_swapchain{};

    std::array<rhi::swapchain_backbuffer_t, NUMBER_OF_BACKBUFFERS> m_swapchain_backbuffers{};

    descriptor_heap_t m_cbv_srv_uav_descriptor_heap{};
    descriptor_heap_t m_rtv_descriptor_heap{};
    descriptor_heap_t m_dsv_descriptor_heap{};

    u8 m_swapchain_backbuffer_index{};

    // Bindless root signature, that is shared by all pipelines.
    ComPtr<ID3D12RootSignature> m_bindless_root_signature{};

    // Mutex used for resource creation.
    std::mutex m_resource_mutex{};

    direct_command_queue_t m_direct_queue{};
    copy_command_queue_t m_copy_queue{};
};

template <typename T>
inline constant_buffer_t<T> renderer_t::create_constant_buffer(const std::wstring_view buffer_name)
{
    ZoneScopedC(tracy::Color::AntiqueWhite);
    constant_buffer_t<T> constant_buffer = {};

    const D3D12_HEAP_PROPERTIES upload_heap_properties = {
        .Type = D3D12_HEAP_TYPE_UPLOAD,
        .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
        .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
        .CreationNodeMask = 0u,
        .VisibleNodeMask = 0u,
    };

    const D3D12_RESOURCE_DESC buffer_resource_desc = {
        .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Alignment = 0u,
        .Width = sizeof(T),
        .Height = 1u,
        .DepthOrArraySize = 1u,
        .MipLevels = 1u,
        .Format = DXGI_FORMAT_UNKNOWN,
        .SampleDesc = {1u, 0u},
        .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
        .Flags = D3D12_RESOURCE_FLAG_NONE,
    };

    throw_if_failed(m_device->CreateCommittedResource(
        &upload_heap_properties, D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES | D3D12_HEAP_FLAG_CREATE_NOT_ZEROED,
        &buffer_resource_desc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr,
        IID_PPV_ARGS(&constant_buffer.m_resource)));

    // Now that a resource is created, copy CPU data to this upload buffer.
    const D3D12_RANGE read_range{.Begin = 0u, .End = 0u};

    throw_if_failed(constant_buffer.m_resource->Map(0u, &read_range, (void **)&constant_buffer.m_resource_mapped_ptr));

    name_d3d12_object(constant_buffer.m_resource.Get(), buffer_name);

    // Create Constant buffer view.
    constant_buffer.m_cbv_index = create_constant_buffer_view(constant_buffer.m_resource.Get(), sizeof(T));

    return constant_buffer;
}

template <typename T, size_t N>
inline std::array<constant_buffer_t<T>, N> renderer_t::create_constant_buffers(const std::wstring_view buffer_name)
{
    std::array<constant_buffer_t<T>, N> constant_buffers{};
    for (size_t i = 0; i < N; i++)
    {
        constant_buffers[i] = create_constant_buffer<T>(std::wstring(buffer_name) + std::to_wstring(i));
    }

    return constant_buffers;
}

} // namespace rhi
