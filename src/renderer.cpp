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

    descriptor_handle_size = device->GetDescriptorHandleIncrementSize(descriptor_heap_type);
}

Renderer::Renderer(const HWND window_handle, const u16 window_width, const u16 window_height)
{
    // Enable the debug layer in debug mode.
    if constexpr (VX_DEBUG_MODE)
    {
        throw_if_failed(D3D12GetDebugInterface(IID_PPV_ARGS(&m_debug_device)));
        m_debug_device->EnableDebugLayer();
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
    throw_if_failed(D3D12CreateDevice(m_dxgi_adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device)));

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
    m_direct_queue.create(m_device.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT);
    m_copy_queue.create(m_device.Get(), D3D12_COMMAND_LIST_TYPE_COPY);

    // Create descriptor heaps.
    m_cbv_srv_uav_descriptor_heap = DescriptorHeap{};
    m_cbv_srv_uav_descriptor_heap.create(m_device.Get(), D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1,
                                         D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                         D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);

    m_rtv_descriptor_heap = DescriptorHeap{};
    m_rtv_descriptor_heap.create(m_device.Get(), 10u, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);

    m_dsv_descriptor_heap = DescriptorHeap{};
    m_dsv_descriptor_heap.create(m_device.Get(), 1u, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);

    // Create the dxgi swapchain.
    {
        Microsoft::WRL::ComPtr<IDXGISwapChain1> swapchain_1{};
        const DXGI_SWAP_CHAIN_DESC1 swapchain_desc = {
            .Width = window_width,
            .Height = window_height,
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

StructuredBuffer Renderer::create_structured_buffer(const void *data, const size_t stride, const size_t num_elements)
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
        &upload_heap_properties, D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES, &buffer_resource_desc,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&intermediate_buffer_resource)));

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
        &default_heap_properties, D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES, &buffer_resource_desc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&buffer_resource)));

    std::scoped_lock<std::mutex> scoped_lock(m_resource_mutex);

    std::unique_lock<std::mutex> ul(m_copy_queue.m_queue_lock);
    m_copy_queue.m_cv.wait(ul, [&] { return m_copy_queue.m_is_command_list_closed == false; });

    m_intermediate_resources.emplace_back(std::move(intermediate_buffer_resource));
    m_resources.emplace_back(std::move(buffer_resource));

    m_copy_queue.m_command_list->CopyResource(m_resources.back().Get(), m_intermediate_resources.back().Get());

    // Create structured buffer view.
    size_t srv_index = create_shader_resource_view(m_resources.size() - 1u, stride, num_elements);

    return StructuredBuffer{
        .resource_index = m_resources.size() - 1u,
        .srv_index = srv_index,
    };
}

ConstantBuffer Renderer::create_constant_buffer(const size_t size_in_bytes)
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
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&buffer_resource)));

    // Now that a resource is created, copy CPU data to this upload buffer.
    const D3D12_RANGE read_range{.Begin = 0u, .End = 0u};

    throw_if_failed(buffer_resource->Map(0u, &read_range, (void **)&resource_ptr));

    std::scoped_lock<std::mutex> scoped_lock(m_resource_mutex);

    m_resources.emplace_back(std::move(buffer_resource));

    // Create Constant buffer view.
    size_t cbv_index = create_constant_buffer_view(m_resources.size() - 1u, size_in_bytes);

    return ConstantBuffer{
        .resource_index = m_resources.size() - 1u,
        .cbv_index = cbv_index,
        .size_in_bytes = size_in_bytes,
        .resource_mapped_ptr = resource_ptr,
    };
}

size_t Renderer::create_constant_buffer_view(const size_t buffer_resource_index, const size_t size)
{
    const D3D12_CPU_DESCRIPTOR_HANDLE handle = m_cbv_srv_uav_descriptor_heap.current_cpu_descriptor_handle;

    const D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {
        .BufferLocation = m_resources[buffer_resource_index]->GetGPUVirtualAddress(),
        .SizeInBytes = static_cast<UINT>(size),
    };

    m_device->CreateConstantBufferView(&cbv_desc, handle);

    const size_t cbv_index = m_cbv_srv_uav_descriptor_heap.current_descriptor_handle_index;

    m_cbv_srv_uav_descriptor_heap.offset_current_descriptor_handles();

    return cbv_index;
}

size_t Renderer::create_shader_resource_view(const size_t buffer_resource_index, const size_t stride,
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

    m_device->CreateShaderResourceView(m_resources[buffer_resource_index].Get(), &srv_desc, handle);

    const size_t srv_index = m_cbv_srv_uav_descriptor_heap.current_descriptor_handle_index;

    m_cbv_srv_uav_descriptor_heap.offset_current_descriptor_handles();

    return srv_index;
}
