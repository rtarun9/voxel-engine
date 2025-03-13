#include "voxel-engine/rhi/renderer.hpp"

#include "tracy/Tracy.hpp"

// Agility SDK setup.
// Setting the Agility SDK parameters.
extern "C"
{
    __declspec(dllexport) extern const UINT D3D12SDKVersion = 614u;
}

extern "C"
{
    __declspec(dllexport) extern const char *D3D12SDKPath = ".\\D3D12\\";
}

namespace rhi
{
renderer_t::renderer_t(const HWND window_handle, const u32 window_width, const u32 window_height)
{
    // Enable the debug layer in debug mode.
    if constexpr (VX_DEBUG_MODE)
    {
        throw_if_failed(D3D12GetDebugInterface(IID_PPV_ARGS(&m_debug_device)));
        m_debug_device->EnableDebugLayer();

        ComPtr<ID3D12Debug1> debug_1{};
        throw_if_failed(m_debug_device->QueryInterface(IID_PPV_ARGS(&debug_1)));
        debug_1->SetEnableSynchronizedCommandQueueValidation(TRUE);

        // Note : This cannot be set, because of the warning message
        // contains a shader op (Draw/Dispatch/ExecuteIndirect) recorded while using Shader Patch Mode NONE, or contains
        // an ExecuteIndirect that changes VB/IB/Root bindings. Hence, all further GPU-based validation may
        // undervalidate or produce true GBV errors with imprecise tracked state (labelled with 'Possibly imprecise')
        // for resources in the COMMON state or Promoted-from-COMMON state at the time of execute.

        debug_1->SetEnableGPUBasedValidation(FALSE);
    }

    // Create the DXGI Factory so we get access to DXGI objects (like adapters).
    u32 dxgi_factory_creation_flags = 0u;

    if constexpr (VX_DEBUG_MODE)
    {
        dxgi_factory_creation_flags = DXGI_CREATE_FACTORY_DEBUG;
    }

    throw_if_failed(CreateDXGIFactory2(dxgi_factory_creation_flags, IID_PPV_ARGS(&m_dxgi_factory)));

    // Get the adapter with best performance. Print the selected adapter's details to console.
    throw_if_failed(m_dxgi_factory->EnumAdapterByGpuPreference(0u, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                               IID_PPV_ARGS(&m_dxgi_adapter)));

    DXGI_ADAPTER_DESC adapter_desc{};
    throw_if_failed(m_dxgi_adapter->GetDesc(&adapter_desc));
    printf("Selected adapter desc :: %ls.\n", adapter_desc.Description);

    // Create the d3d12 device (logical adapter : All d3d objects require d3d12 device for creation).
    throw_if_failed(D3D12CreateDevice(m_dxgi_adapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&m_device)));
    name_d3d12_object(m_device.Get(), L"D3D12 device");

    // In debug mode, setup the info queue so breakpoint is placed whenever a error / warning occurs that is d3d
    // related.
    ComPtr<ID3D12InfoQueue> info_queue{};
    if constexpr (VX_DEBUG_MODE)
    {
        throw_if_failed(m_device.As(&info_queue));

        throw_if_failed(info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE));
        throw_if_failed(info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE));
        throw_if_failed(info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE));
    }

    // Setup the copy queue & direct queue primitives.
    m_direct_queue.create(m_device.Get());
    m_copy_queue.create(m_device.Get());

    // Create descriptor heaps.
    m_cbv_srv_uav_descriptor_heap.create(m_device.Get(), D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1,
                                         D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                         D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, L"CBV SRV UAV Descriptor Heap");

    m_rtv_descriptor_heap.create(m_device.Get(), 10u, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
                                 L"RTV Descriptor Heap");

    m_dsv_descriptor_heap.create(m_device.Get(), 1u, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
                                 L"DSV Descriptor Heap");

    // Create the dxgi swapchain.
    ComPtr<IDXGISwapChain1> swapchain_1{};
    const DXGI_SWAP_CHAIN_DESC1 swapchain_desc = {
        .Width = static_cast<UINT>(window_width),
        .Height = static_cast<UINT>(window_height),
        .Format = BACKBUFFER_FORMAT,
        .Stereo = FALSE,
        .SampleDesc = {1, 0},
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = NUMBER_OF_BACKBUFFERS,
        .Scaling = DXGI_SCALING_NONE,
        .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
        .AlphaMode = DXGI_ALPHA_MODE_IGNORE,
        .Flags = 0u,
    };
    throw_if_failed(m_dxgi_factory->CreateSwapChainForHwnd(m_direct_queue.m_command_queue.Get(), window_handle,
                                                           &swapchain_desc, nullptr, nullptr, &swapchain_1));

    throw_if_failed(swapchain_1.As(&m_swapchain));

    // Create the render target view for the swapchain back buffer.

    for (u8 i = 0; i < NUMBER_OF_BACKBUFFERS; i++)
    {
        descriptor_handle_t rtv_descriptor_handle = m_rtv_descriptor_heap.get_then_offset_current_descriptor_handle();

        ComPtr<ID3D12Resource> swapchain_resource{};
        throw_if_failed(m_swapchain->GetBuffer(i, IID_PPV_ARGS(&swapchain_resource)));
        m_swapchain_backbuffers[i].m_rtv_cpu_descriptor_handle = rtv_descriptor_handle.m_cpu_descriptor_handle;

        m_device->CreateRenderTargetView(swapchain_resource.Get(), nullptr,
                                         m_swapchain_backbuffers[i].m_rtv_cpu_descriptor_handle);

        m_swapchain_backbuffers[i].m_resource = (std::move(swapchain_resource));
    }

    m_swapchain_backbuffer_index = static_cast<u8>(m_swapchain->GetCurrentBackBufferIndex());

    // Create and setup the bindless root signature that is shared by all pipelines.
    const D3D12_ROOT_PARAMETER1 shader_constant_root_parameter = {
        .ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
        .Constants =
            {
                .ShaderRegister = 0u,
                .RegisterSpace = 0u,
                .Num32BitValues = 64u,
            },
        .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL,
    };

    ComPtr<ID3DBlob> bindless_root_signature_blob{};
    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc = {
        .Version = D3D_ROOT_SIGNATURE_VERSION_1_1,
        .Desc_1_1 =
            {
                .NumParameters = 1u,
                .pParameters = &shader_constant_root_parameter,
                .NumStaticSamplers = 0u,
                .pStaticSamplers = nullptr,
                .Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED,
            },
    };

    // Serialize root signature.
    throw_if_failed(D3D12SerializeVersionedRootSignature(&root_signature_desc, &bindless_root_signature_blob, nullptr));
    throw_if_failed(m_device->CreateRootSignature(0u, bindless_root_signature_blob->GetBufferPointer(),
                                                  bindless_root_signature_blob->GetBufferSize(),
                                                  IID_PPV_ARGS(&m_bindless_root_signature)));
    name_d3d12_object(m_bindless_root_signature.Get(), L"D3D12 bindless root signature");
}

renderer_t::index_buffer_with_intermediate_resource_t renderer_t::create_index_buffer(
    const void *data, const size_t stride, const u32 indices_count, const std::wstring_view buffer_name)
{
    ZoneScoped;
    assert(data);

    index_buffer_with_intermediate_resource_t result = {};

    const size_t size_in_bytes = stride * indices_count;

    // First, create a upload buffer (that is placed in memory accesible by both GPU and CPU).
    // Then create a GPU only buffer, and copy data from the previous buffer to GPU only one.
    const D3D12_HEAP_PROPERTIES upload_heap_properties = {
        .Type = D3D12_HEAP_TYPE_UPLOAD,
        .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
        .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
        .CreationNodeMask = 0u,
        .VisibleNodeMask = 0u,
    };

    const D3D12_RESOURCE_DESC buffer_resource_desc = {
        .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Width = size_in_bytes,
        .Height = 1u,
        .DepthOrArraySize = 1u,
        .MipLevels = 1u,
        .Format = DXGI_FORMAT_UNKNOWN,
        .SampleDesc = {1u, 0u},
        .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
        .Flags = D3D12_RESOURCE_FLAG_NONE,
    };

    throw_if_failed(m_device->CreateCommittedResource(
        &upload_heap_properties, D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES, &buffer_resource_desc,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&result.m_intermediate_resource)));

    // Now that a resource is created, copy CPU data to this upload buffer.
    const D3D12_RANGE read_range{.Begin = 0u, .End = 0u};

    u8 *resource_ptr{};

    throw_if_failed(result.m_intermediate_resource->Map(0u, &read_range, (void **)&resource_ptr));

    memcpy(resource_ptr, data, size_in_bytes);

    // Create the final resource and transfer the data from upload buffer to the final buffer.
    // The heap type is : Default (no CPU access).
    const D3D12_HEAP_PROPERTIES default_heap_properties = {
        .Type = D3D12_HEAP_TYPE_DEFAULT,
        .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
        .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
        .CreationNodeMask = 0u,
        .VisibleNodeMask = 0u,
    };

    throw_if_failed(m_device->CreateCommittedResource(
        &default_heap_properties, D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES, &buffer_resource_desc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&result.m_index_buffer.m_resource)));

    name_d3d12_object(result.m_index_buffer.m_resource.Get(), buffer_name);
    name_d3d12_object(result.m_intermediate_resource.Get(),
                      std::wstring(buffer_name) + std::wstring(L" [intermediate]"));

    std::scoped_lock<std::mutex> scoped_lock(m_resource_mutex);

    auto command_allocator_list_pair = m_copy_queue.get_command_allocator_list_pair(m_device.Get());

    command_allocator_list_pair.m_command_list->CopyResource(result.m_index_buffer.m_resource.Get(),
                                                             result.m_intermediate_resource.Get());
    m_copy_queue.execute_command_list(std::move(command_allocator_list_pair));

    result.m_index_buffer.m_index_buffer_view = {
        .BufferLocation = result.m_index_buffer.m_resource->GetGPUVirtualAddress(),
        .SizeInBytes = static_cast<UINT>(size_in_bytes),
        .Format = DXGI_FORMAT_R16_UINT,
    };

    result.m_index_buffer.m_indices_count = indices_count;

    return result;
}

renderer_t::structured_buffer_with_intermediate_resource_t renderer_t::create_structured_buffer(
    const void *data, const size_t stride, const u32 num_elements, const std::wstring_view buffer_name)
{
    ZoneScoped;
    assert(data);

    const size_t size_in_bytes = stride * num_elements;

    u8 *resource_ptr{};
    structured_buffer_with_intermediate_resource_t result = {};

    // First, create a upload buffer (that is placed in memory accesible by both GPU and CPU).
    // Then create a GPU only buffer, and copy data from the previous buffer to GPU only one.
    const D3D12_HEAP_PROPERTIES upload_heap_properties = {
        .Type = D3D12_HEAP_TYPE_UPLOAD,
        .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
        .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
        .CreationNodeMask = 0u,
        .VisibleNodeMask = 0u,
    };

    const D3D12_RESOURCE_DESC buffer_resource_desc = {
        .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Width = size_in_bytes,
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
        IID_PPV_ARGS(&result.m_intermediate_resource)));

    // Now that a resource is created, copy CPU data to this upload buffer.
    const D3D12_RANGE read_range{.Begin = 0u, .End = 0u};

    throw_if_failed(result.m_intermediate_resource->Map(0u, &read_range, (void **)&resource_ptr));

    memcpy(resource_ptr, data, size_in_bytes);

    // Create the final resource and transfer the data from upload buffer to the final buffer.
    // The heap type is : Default (no CPU access).
    const D3D12_HEAP_PROPERTIES default_heap_properties = {
        .Type = D3D12_HEAP_TYPE_DEFAULT,
        .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
        .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
        .CreationNodeMask = 0u,
        .VisibleNodeMask = 0u,
    };

    throw_if_failed(m_device->CreateCommittedResource(
        &default_heap_properties, D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES | D3D12_HEAP_FLAG_CREATE_NOT_ZEROED,
        &buffer_resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
        IID_PPV_ARGS(&result.m_structured_buffer.m_resource)));

    name_d3d12_object(result.m_structured_buffer.m_resource.Get(), buffer_name);
    name_d3d12_object(result.m_intermediate_resource.Get(),
                      std::wstring(buffer_name) + std::wstring(L" [intermediate]"));

    std::scoped_lock<std::mutex> scoped_lock(m_resource_mutex);

    auto command_allocator_list_pair = m_copy_queue.get_command_allocator_list_pair(m_device.Get());

    command_allocator_list_pair.m_command_list->CopyResource(result.m_structured_buffer.m_resource.Get(),
                                                             result.m_intermediate_resource.Get());
    m_copy_queue.execute_command_list(std::move(command_allocator_list_pair));

    // Create structured buffer view.
    result.m_structured_buffer.m_srv_index =
        create_shader_resource_view(result.m_structured_buffer.m_resource.Get(), stride, num_elements);

    return result;
}

upload_structured_buffer_t renderer_t::create_upload_structured_buffer(const size_t stride, const u32 num_elements,
                                                                       const std::wstring_view buffer_name)
{
    ZoneScoped;
    const size_t size_in_bytes = stride * num_elements;

    upload_structured_buffer_t result = {};

    // First, create a upload buffer (that is placed in memory accesible by both GPU and CPU).
    // Then create a GPU only buffer, and copy data from the previous buffer to GPU only one.
    const D3D12_HEAP_PROPERTIES upload_heap_properties = {
        .Type = D3D12_HEAP_TYPE_UPLOAD,
        .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
        .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
        .CreationNodeMask = 0u,
        .VisibleNodeMask = 0u,
    };

    const D3D12_RESOURCE_DESC buffer_resource_desc = {
        .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Width = size_in_bytes,
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
        IID_PPV_ARGS(&result.m_upload_resource)));

    // Now that a resource is created, copy CPU data to this upload buffer.
    const D3D12_RANGE read_range{.Begin = 0u, .End = 0u};

    throw_if_failed(result.m_upload_resource->Map(0u, &read_range, (void **)&result.m_upload_resource_mapped_ptr));

    // Create the final resource and transfer the data from upload buffer to the final buffer.
    // The heap type is : Default (no CPU access).
    const D3D12_HEAP_PROPERTIES default_heap_properties = {
        .Type = D3D12_HEAP_TYPE_DEFAULT,
        .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
        .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
        .CreationNodeMask = 0u,
        .VisibleNodeMask = 0u,
    };

    throw_if_failed(m_device->CreateCommittedResource(
        &default_heap_properties, D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES | D3D12_HEAP_FLAG_CREATE_NOT_ZEROED,
        &buffer_resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&result.m_default_resource)));

    name_d3d12_object(result.m_upload_resource.Get(), std::wstring(buffer_name) + std::wstring(L" upload resource"));
    name_d3d12_object(result.m_default_resource.Get(), std::wstring(buffer_name) + std::wstring(L" default resource"));

    std::scoped_lock<std::mutex> scoped_lock(m_resource_mutex);

    // Create structured buffer view.
    result.m_default_resource_srv_index =
        create_shader_resource_view(result.m_upload_resource.Get(), stride, num_elements);

    return result;
}

void renderer_t::update_upload_structured_buffer(upload_structured_buffer_t &upload_structured_buffer, const void *data,
                                                 const size_t size_in_bytes, const size_t offset_in_bytes)
{
    assert(upload_structured_buffer.m_upload_resource_mapped_ptr != nullptr);
    memcpy((void *)((u8 *)upload_structured_buffer.m_upload_resource_mapped_ptr + offset_in_bytes), (void *)(data),
           size_in_bytes);

    std::scoped_lock<std::mutex> scoped_lock(m_resource_mutex);

    auto command_allocator_list_pair = m_copy_queue.get_command_allocator_list_pair(m_device.Get());

    command_allocator_list_pair.m_command_list->CopyBufferRegion(
        upload_structured_buffer.m_default_resource.Get(), offset_in_bytes,
        upload_structured_buffer.m_upload_resource.Get(), offset_in_bytes, size_in_bytes);

    m_copy_queue.execute_command_list(std::move(command_allocator_list_pair));
}

command_buffer_t renderer_t::create_command_buffer(const size_t stride, const size_t max_number_of_elements,
                                                   const std::wstring_view buffer_name)
{
    ZoneScoped;

    command_buffer_t result = {};
    // Note that counter offset must be multiple of d3d12 uav counter placement alignment.
    result.m_counter_offset =
        round_up_to_multiple(stride * max_number_of_elements, D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT);

    const size_t size_in_bytes = result.m_counter_offset + 4u;

    // First, create a upload buffer (that is placed in memory accesible by both GPU and CPU).
    // Then create a GPU only buffer, and copy data from the previous buffer to GPU only one.
    const D3D12_HEAP_PROPERTIES upload_heap_properties = {
        .Type = D3D12_HEAP_TYPE_UPLOAD,
        .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
        .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
        .CreationNodeMask = 0u,
        .VisibleNodeMask = 0u,
    };

    const D3D12_RESOURCE_DESC upload_buffer_resource_desc = {
        .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Width = size_in_bytes,
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
        &upload_buffer_resource_desc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr,
        IID_PPV_ARGS(&result.m_upload_resource)));

    // Now that a resource is created, copy CPU data to this upload buffer.
    const D3D12_RANGE read_range{.Begin = 0u, .End = 0u};

    throw_if_failed(result.m_upload_resource->Map(0u, &read_range, (void **)&result.m_upload_resource_mapped_ptr));

    // Create a small resource that only has a single uint -> whose value is always zero.
    // This is used each frame to reset the counter value to 0 for the uav.
    {
        const D3D12_RESOURCE_DESC zeroed_counter_buffer_resource_desc = {
            .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
            .Width = 4u,
            .Height = 1u,
            .DepthOrArraySize = 1u,
            .MipLevels = 1u,
            .Format = DXGI_FORMAT_UNKNOWN,
            .SampleDesc = {1u, 0u},
            .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
            .Flags = D3D12_RESOURCE_FLAG_NONE,
        };

        u8 *zeroed_counter_buffer_ptr = nullptr;
        throw_if_failed(m_device->CreateCommittedResource(
            &upload_heap_properties, D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES | D3D12_HEAP_FLAG_CREATE_NOT_ZEROED,
            &upload_buffer_resource_desc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr,
            IID_PPV_ARGS(&result.m_zeroed_counter_buffer_resource)));

        throw_if_failed(
            result.m_zeroed_counter_buffer_resource->Map(0u, &read_range, (void **)&zeroed_counter_buffer_ptr));
        u32 zero = 0u;
        memcpy(zeroed_counter_buffer_ptr, &zero, sizeof(u32));
        result.m_zeroed_counter_buffer_resource->Unmap(0u, nullptr);
    }

    const D3D12_RESOURCE_DESC buffer_resource_desc = {
        .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Width = size_in_bytes,
        .Height = 1u,
        .DepthOrArraySize = 1u,
        .MipLevels = 1u,
        .Format = DXGI_FORMAT_UNKNOWN,
        .SampleDesc = {1u, 0u},
        .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
        .Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
    };

    // Create the final resource and transfer the data from upload buffer to the final buffer.
    // The heap type is : Default (no CPU access).
    const D3D12_HEAP_PROPERTIES default_heap_properties = {
        .Type = D3D12_HEAP_TYPE_DEFAULT,
        .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
        .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
        .CreationNodeMask = 0u,
        .VisibleNodeMask = 0u,
    };

    throw_if_failed(m_device->CreateCommittedResource(
        &default_heap_properties, D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES | D3D12_HEAP_FLAG_CREATE_NOT_ZEROED,
        &buffer_resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&result.m_default_resource)));

    name_d3d12_object(result.m_default_resource.Get(), buffer_name);
    name_d3d12_object(result.m_upload_resource.Get(), std::wstring(buffer_name) + std::wstring(L" [intermediate]"));
    name_d3d12_object(result.m_zeroed_counter_buffer_resource.Get(),
                      std::wstring(buffer_name) + std::wstring(L" [zeroed counter buffer]"));

    // Create the SRV.
    result.m_upload_resource_srv_index =
        create_shader_resource_view(result.m_upload_resource.Get(), stride, max_number_of_elements);

    // Create the UAV.
    result.m_default_resource_uav_index = create_unordered_access_view(
        result.m_default_resource.Get(), stride, max_number_of_elements, true, result.m_counter_offset);

    return result;
}

u32 renderer_t::create_constant_buffer_view(ID3D12Resource *const resource, const size_t size)
{
    ZoneScoped;
    assert(resource);

    descriptor_handle_t descriptor_handle = m_cbv_srv_uav_descriptor_heap.get_then_offset_current_descriptor_handle();
    const D3D12_CPU_DESCRIPTOR_HANDLE handle = descriptor_handle.m_cpu_descriptor_handle;

    const D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {
        .BufferLocation = resource->GetGPUVirtualAddress(),
        .SizeInBytes = static_cast<UINT>(size),
    };

    m_device->CreateConstantBufferView(&cbv_desc, handle);

    const size_t cbv_index = descriptor_handle.m_index;

    return cbv_index;
}

u32 renderer_t::create_shader_resource_view(ID3D12Resource *const resource, const size_t stride,
                                            const size_t num_elements)
{
    ZoneScoped;
    assert(resource);

    descriptor_handle_t descriptor_handle = m_cbv_srv_uav_descriptor_heap.get_then_offset_current_descriptor_handle();
    const D3D12_CPU_DESCRIPTOR_HANDLE handle = descriptor_handle.m_cpu_descriptor_handle;

    const D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {
        .Format = DXGI_FORMAT_UNKNOWN,
        .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
        .Buffer{
            .FirstElement = 0u,
            .NumElements = static_cast<UINT>(num_elements),
            .StructureByteStride = static_cast<UINT>(stride),
        },
    };

    m_device->CreateShaderResourceView(resource, &srv_desc, handle);

    const size_t srv_index = descriptor_handle.m_index;

    return srv_index;
}

u32 renderer_t::create_unordered_access_view(ID3D12Resource *const resource, const size_t stride,
                                             const size_t num_elements, const bool use_counter,
                                             const size_t counter_offset)
{
    ZoneScoped;
    assert(resource);

    descriptor_handle_t descriptor_handle = m_cbv_srv_uav_descriptor_heap.get_then_offset_current_descriptor_handle();

    const D3D12_CPU_DESCRIPTOR_HANDLE handle = descriptor_handle.m_cpu_descriptor_handle;

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {
        .Format = DXGI_FORMAT_UNKNOWN,
        .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
        .Buffer{
            .FirstElement = 0u,
            .NumElements = static_cast<UINT>(num_elements),
            .StructureByteStride = static_cast<UINT>(stride),
            .CounterOffsetInBytes = 0u,
            .Flags = D3D12_BUFFER_UAV_FLAGS::D3D12_BUFFER_UAV_FLAG_NONE,
        },
    };

    if (!use_counter)
    {
        m_device->CreateUnorderedAccessView(resource, nullptr, &uav_desc, handle);
    }
    else
    {
        uav_desc.Buffer.CounterOffsetInBytes = counter_offset;
        m_device->CreateUnorderedAccessView(resource, resource, &uav_desc, handle);
    }

    const size_t uav_index = descriptor_handle.m_index;

    return uav_index;
}

} // namespace rhi
