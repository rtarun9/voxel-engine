#include "voxel-engine/renderer.hpp"

// Agility SDK setup.
// Setting the Agility SDK parameters.
extern "C"
{
    __declspec(dllexport) extern const UINT D3D12SDKVersion = 711u;
}

extern "C"
{
    __declspec(dllexport) extern const char *D3D12SDKPath = ".\\D3D12\\";
}

D3D12_GPU_DESCRIPTOR_HANDLE Renderer::DescriptorHeap::get_gpu_descriptor_handle_at_index(const size_t index) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE handle = descriptor_heap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += index * descriptor_handle_size;

    return handle;
}

D3D12_CPU_DESCRIPTOR_HANDLE Renderer::DescriptorHeap::get_cpu_descriptor_handle_at_index(const size_t index) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = descriptor_heap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += index * descriptor_handle_size;

    return handle;
}

void Renderer::DescriptorHeap::offset_current_descriptor_handles()
{
    current_cpu_descriptor_handle.ptr += descriptor_handle_size;
    current_gpu_descriptor_handle.ptr += descriptor_handle_size;

    current_descriptor_handle_index++;
}

void Renderer::DescriptorHeap::create(ID3D12Device *const device, const size_t num_descriptors,
                                      const D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type,
                                      const D3D12_DESCRIPTOR_HEAP_FLAGS descriptor_heap_flags)
{
    const D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_desc = {
        .Type = descriptor_heap_type,
        .NumDescriptors = static_cast<UINT>(num_descriptors),
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

Renderer::Renderer(const HWND window_handle, const u16 window_width, const u16 window_height)
{
    // Enable the debug layer in debug mode.
    if constexpr (VX_DEBUG_MODE)
    {
        throw_if_failed(D3D12GetDebugInterface(IID_PPV_ARGS(&m_debug_device)));
        m_debug_device->EnableDebugLayer();

        Microsoft::WRL::ComPtr<ID3D12Debug1> debug_1{};
        throw_if_failed(m_debug_device->QueryInterface(IID_PPV_ARGS(&debug_1)));
        debug_1->SetEnableGPUBasedValidation(TRUE);
        debug_1->SetEnableSynchronizedCommandQueueValidation(TRUE);
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

    // In debug mode, setup the info queue so breakpoint is placed whenever a error / warning occurs that is d3d
    // related.
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> info_queue{};
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
                                         D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);

    m_rtv_descriptor_heap.create(m_device.Get(), 10u, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);

    m_dsv_descriptor_heap.create(m_device.Get(), 1u, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);

    // Create the dxgi swapchain.
    {
        Microsoft::WRL::ComPtr<IDXGISwapChain1> swapchain_1{};
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
    }

    // Create the render target view for the swapchain back buffer.
    for (u8 i = 0; i < NUMBER_OF_BACKBUFFERS; i++)
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> swapchain_resource{};
        throw_if_failed(m_swapchain->GetBuffer(i, IID_PPV_ARGS(&swapchain_resource)));
        m_swapchain_backbuffer_cpu_descriptor_handles[i] = m_rtv_descriptor_heap.get_cpu_descriptor_handle_at_index(i);

        m_device->CreateRenderTargetView(swapchain_resource.Get(), nullptr,
                                         m_swapchain_backbuffer_cpu_descriptor_handles[i]);

        m_swapchain_backbuffer_resources[i] = (std::move(swapchain_resource));
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

    Microsoft::WRL::ComPtr<ID3DBlob> bindless_root_signature_blob{};
    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc = {
        .Version = D3D_ROOT_SIGNATURE_VERSION_1_1,
        .Desc_1_1 =
            {
                .NumParameters = 1u,
                .pParameters = &shader_constant_root_parameter,
                .NumStaticSamplers = 0u,
                .pStaticSamplers = nullptr,
                .Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
                         D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED,
            },
    };

    // Serialize root signature.
    throw_if_failed(D3D12SerializeVersionedRootSignature(&root_signature_desc, &bindless_root_signature_blob, nullptr));
    throw_if_failed(m_device->CreateRootSignature(0u, bindless_root_signature_blob->GetBufferPointer(),
                                                  bindless_root_signature_blob->GetBufferSize(),
                                                  IID_PPV_ARGS(&m_bindless_root_signature)));
}

StructuredBuffer Renderer::create_structured_buffer(const void *data, const size_t stride, const size_t num_elements,
                                                    const std::wstring_view buffer_name)
{
    // note(rtarun9) : Figure out how to handle these invalid cases.
    if (data == nullptr)
    {
        return StructuredBuffer{};
    }

    const size_t size_in_bytes = stride * num_elements;

    u8 *resource_ptr{};
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer_resource{};
    Microsoft::WRL::ComPtr<ID3D12Resource> intermediate_buffer_resource{};

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
        IID_PPV_ARGS(&intermediate_buffer_resource)));

    // Now that a resource is created, copy CPU data to this upload buffer.
    const D3D12_RANGE read_range{.Begin = 0u, .End = 0u};

    throw_if_failed(intermediate_buffer_resource->Map(0u, &read_range, (void **)&resource_ptr));

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
        &buffer_resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&buffer_resource)));

    std::scoped_lock<std::mutex> scoped_lock(m_resource_mutex);

    m_intermediate_resources.emplace_back(std::move(intermediate_buffer_resource));
    m_resources.emplace_back(std::move(buffer_resource));

    name_d3d12_object(m_resources.back().Get(), buffer_name);
    name_d3d12_object(m_intermediate_resources.back().Get(),
                      std::wstring(buffer_name) + std::wstring(L" [intermediate]"));

    auto command_allocator_list_pair = m_copy_queue.get_command_allocator_list_pair(m_device.Get());

    command_allocator_list_pair.m_command_list->CopyResource(m_resources.back().Get(),
                                                             m_intermediate_resources.back().Get());
    m_copy_queue.execute_command_list(std::move(command_allocator_list_pair));

    // Create structured buffer view.
    size_t srv_index = create_shader_resource_view(m_resources.back().Get(), stride, num_elements);

    return StructuredBuffer{
        .resource_index = m_resources.size() - 1u,
        .srv_index = srv_index,
    };
}

ConstantBuffer Renderer::create_constant_buffer(const size_t size_in_bytes, const std::wstring_view buffer_name)
{
    u8 *resource_ptr{};
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer_resource{};

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
        IID_PPV_ARGS(&buffer_resource)));

    // Now that a resource is created, copy CPU data to this upload buffer.
    const D3D12_RANGE read_range{.Begin = 0u, .End = 0u};

    throw_if_failed(buffer_resource->Map(0u, &read_range, (void **)&resource_ptr));

    std::scoped_lock<std::mutex> scoped_lock(m_resource_mutex);

    m_resources.emplace_back(std::move(buffer_resource));
    name_d3d12_object(m_resources.back().Get(), buffer_name);

    // Create Constant buffer view.
    size_t cbv_index = create_constant_buffer_view(m_resources.back().Get(), size_in_bytes);

    return ConstantBuffer{
        .resource_index = m_resources.size() - 1u,
        .cbv_index = cbv_index,
        .size_in_bytes = size_in_bytes,
        .resource_mapped_ptr = resource_ptr,
    };
}

CommandBuffer Renderer::create_command_buffer(const size_t stride, const size_t max_number_of_elements,
                                              const std::wstring_view buffer_name)
{
    // Note that counter offset must be multiple of d3d12 uav counter placement alignment.
    size_t counter_offset =
        round_up_to_multiple(stride * max_number_of_elements, D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT);
    const size_t size_in_bytes = counter_offset + 4u;

    u8 *resource_ptr{};
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer_resource{};
    Microsoft::WRL::ComPtr<ID3D12Resource> intermediate_buffer_resource{};
    Microsoft::WRL::ComPtr<ID3D12Resource> zeroed_counter_buffer_resource{};

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
        IID_PPV_ARGS(&intermediate_buffer_resource)));

    // Now that a resource is created, copy CPU data to this upload buffer.
    const D3D12_RANGE read_range{.Begin = 0u, .End = 0u};

    throw_if_failed(intermediate_buffer_resource->Map(0u, &read_range, (void **)&resource_ptr));

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
            IID_PPV_ARGS(&zeroed_counter_buffer_resource)));

        throw_if_failed(zeroed_counter_buffer_resource->Map(0u, &read_range, (void **)&zeroed_counter_buffer_ptr));
        u32 zero = 0u;
        memcpy(zeroed_counter_buffer_ptr, &zero, sizeof(u32));
        zeroed_counter_buffer_resource->Unmap(0u, nullptr);
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
        &buffer_resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&buffer_resource)));

    name_d3d12_object(buffer_resource.Get(), buffer_name);
    name_d3d12_object(intermediate_buffer_resource.Get(), std::wstring(buffer_name) + std::wstring(L" [intermediate]"));

    // Create the SRV.
    const size_t upload_resource_srv_index =
        create_shader_resource_view(intermediate_buffer_resource.Get(), stride, max_number_of_elements);

    // Create the UAV.
    const size_t default_resource_uav_index =
        create_unordered_access_view(buffer_resource.Get(), stride, max_number_of_elements, true, counter_offset);

    return CommandBuffer{
        .default_resource = buffer_resource,
        .upload_resource = intermediate_buffer_resource,
        .zeroed_counter_buffer_resource = zeroed_counter_buffer_resource,
        .upload_resource_mapped_ptr = resource_ptr,
        .upload_resource_srv_index = upload_resource_srv_index,
        .default_resource_uav_index = default_resource_uav_index,
        .counter_offset = counter_offset,
    };
}

size_t Renderer::create_constant_buffer_view(ID3D12Resource *const resource, const size_t size)
{
    const D3D12_CPU_DESCRIPTOR_HANDLE handle = m_cbv_srv_uav_descriptor_heap.current_cpu_descriptor_handle;

    const D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {
        .BufferLocation = resource->GetGPUVirtualAddress(),
        .SizeInBytes = static_cast<UINT>(size),
    };

    m_device->CreateConstantBufferView(&cbv_desc, handle);

    const size_t cbv_index = m_cbv_srv_uav_descriptor_heap.current_descriptor_handle_index;

    m_cbv_srv_uav_descriptor_heap.offset_current_descriptor_handles();

    return cbv_index;
}

size_t Renderer::create_shader_resource_view(ID3D12Resource *const resource, const size_t stride,
                                             const size_t num_elements)
{
    const D3D12_CPU_DESCRIPTOR_HANDLE handle = m_cbv_srv_uav_descriptor_heap.current_cpu_descriptor_handle;

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

    const size_t srv_index = m_cbv_srv_uav_descriptor_heap.current_descriptor_handle_index;

    m_cbv_srv_uav_descriptor_heap.offset_current_descriptor_handles();

    return srv_index;
}

size_t Renderer::create_unordered_access_view(ID3D12Resource *const resource, const size_t stride,
                                              const size_t num_elements, const bool use_counter,
                                              const size_t counter_offset)
{
    const D3D12_CPU_DESCRIPTOR_HANDLE handle = m_cbv_srv_uav_descriptor_heap.current_cpu_descriptor_handle;

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

    const size_t uav_index = m_cbv_srv_uav_descriptor_heap.current_descriptor_handle_index;

    m_cbv_srv_uav_descriptor_heap.offset_current_descriptor_handles();

    return uav_index;
}

void Renderer::DirectCommandQueue::create(ID3D12Device *const device)
{
    const D3D12_COMMAND_QUEUE_DESC command_queue_desc = {
        .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
        .Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
        .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
        .NodeMask = 0u,
    };
    throw_if_failed(device->CreateCommandQueue(&command_queue_desc, IID_PPV_ARGS(&m_command_queue)));

    // Create the command allocator (the underlying allocation where gpu commands will be stored after being
    // recorded by command list). Each frame has its own command allocator.
    for (u8 i = 0; i < NUMBER_OF_BACKBUFFERS; i++)
    {
        throw_if_failed(
            device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_command_allocators[i])));
    }

    // Create the graphics command list.
    throw_if_failed(device->CreateCommandList(0u, D3D12_COMMAND_LIST_TYPE_DIRECT, m_command_allocators[0].Get(),
                                              nullptr, IID_PPV_ARGS(&m_command_list)));

    // Create a fence for CPU GPU synchronization.
    throw_if_failed(device->CreateFence(0u, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
}

void Renderer::DirectCommandQueue::reset(const u8 index) const
{
    const auto &allocator = m_command_allocators[index];
    const auto &command_list = m_command_list;

    // Reset command allocator and command list.
    throw_if_failed(allocator->Reset());
    throw_if_failed(command_list->Reset(allocator.Get(), nullptr));
}

void Renderer::DirectCommandQueue::execute_command_list() const
{

    throw_if_failed(m_command_list->Close());

    ID3D12CommandList *const command_lists_to_execute[1] = {m_command_list.Get()};

    m_command_queue->ExecuteCommandLists(1u, command_lists_to_execute);
}

void Renderer::DirectCommandQueue::wait_for_fence_value_at_index(const u8 index)
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

void Renderer::DirectCommandQueue::signal_fence(const u8 index)
{
    throw_if_failed(m_command_queue->Signal(m_fence.Get(), ++m_monotonic_fence_value));
    m_frame_fence_values[index] = m_monotonic_fence_value;
}

void Renderer::DirectCommandQueue::flush_queue()
{

    signal_fence(0);

    for (u32 i = 0; i < NUMBER_OF_BACKBUFFERS; i++)
    {
        m_frame_fence_values[i] = m_monotonic_fence_value;
    }

    wait_for_fence_value_at_index(0);
}

void Renderer::CopyCommandQueue::create(ID3D12Device *const device)
{
    const D3D12_COMMAND_QUEUE_DESC command_queue_desc = {
        .Type = D3D12_COMMAND_LIST_TYPE_COPY,
        .Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
        .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
        .NodeMask = 0u,
    };
    throw_if_failed(device->CreateCommandQueue(&command_queue_desc, IID_PPV_ARGS(&m_command_queue)));

    // Create a fence for CPU GPU synchronization.
    throw_if_failed(device->CreateFence(0u, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
}

Renderer::CopyCommandQueue::CommandAllocatorListPair Renderer::CopyCommandQueue::get_command_allocator_list_pair(
    ID3D12Device *const device)
{
    if (!m_command_allocator_list_queue.empty() &&
        m_command_allocator_list_queue.front().m_fence_value <= m_fence->GetCompletedValue())
    {
        const auto front = m_command_allocator_list_queue.front();
        m_command_allocator_list_queue.pop();

        // Reset list and allocator.
        throw_if_failed(front.m_command_allocator->Reset());
        throw_if_failed(front.m_command_list->Reset(front.m_command_allocator.Get(), nullptr));

        return front;
    }
    else
    {
        CommandAllocatorListPair command_allocator_list_pair{};
        throw_if_failed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY,
                                                       IID_PPV_ARGS(&command_allocator_list_pair.m_command_allocator)));
        throw_if_failed(device->CreateCommandList(0u, D3D12_COMMAND_LIST_TYPE_COPY,
                                                  command_allocator_list_pair.m_command_allocator.Get(), nullptr,
                                                  IID_PPV_ARGS(&command_allocator_list_pair.m_command_list)));

        return command_allocator_list_pair;
    }
}

void Renderer::CopyCommandQueue::execute_command_list(CommandAllocatorListPair &&alloc_list_pair)
{

    throw_if_failed(alloc_list_pair.m_command_list->Close());

    ID3D12CommandList *const command_lists_to_execute[1] = {alloc_list_pair.m_command_list.Get()};

    m_command_queue->ExecuteCommandLists(1u, command_lists_to_execute);

    throw_if_failed(m_command_queue->Signal(m_fence.Get(), ++m_monotonic_fence_value));

    alloc_list_pair.m_fence_value = m_monotonic_fence_value;

    m_command_allocator_list_queue.push(alloc_list_pair);
}

void Renderer::CopyCommandQueue::flush_queue()
{
    throw_if_failed(m_command_queue->Signal(m_fence.Get(), ++m_monotonic_fence_value));
    throw_if_failed(m_fence->SetEventOnCompletion(m_monotonic_fence_value, nullptr));
}
