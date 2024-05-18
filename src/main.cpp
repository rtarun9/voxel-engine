#include "pch.hpp"

#include "renderer.hpp"
#include "shader_compiler.hpp"
#include "types.hpp"
#include "window.hpp"

#include "common.hpp"

#include "camera.hpp"

#include "voxel.hpp"

// Helper function for indexing.
// N is the max number of 'elements' in any direction.
static inline DirectX::XMUINT3 convert_index_to_3d(const u64 index, const u32 N)
{
    // Note that index = x + y * N + z * N * N;
    const u32 z = index / (N * N);
    const u32 index_2d = index - z * N * N;
    const u32 y = index_2d / N;
    const u32 x = index_2d % N;

    return DirectX::XMUINT3{x, y, z};
}

[[maybe_unused]] static inline u64 convert_index_to_1d(const DirectX::XMUINT3 index, const u32 N,
                                                       const u32 bias_value = 0u)
{
    return (bias_value + index.x) + (bias_value + index.y) * N + (bias_value + index.z) * N * N;
}

static inline u64 convert_index_to_1d(const DirectX::XMINT3 index, const u32 N, const u32 bias_value = 0u)
{
    return (bias_value + index.x) + (bias_value + index.y) * N + (bias_value + (u64)index.z) * N * N;
}

int main()
{
    const Window window(1080u, 720u);
    Renderer renderer(window.m_handle, window.m_width, window.m_height);

    // A vector of intermediate resources (required since intermediate buffers need to be in memory until the copy
    // resource and other functions are executed).
    // This vector MUST be erased once GPU resource uploading is completed.
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> intermediate_resources{};
    intermediate_resources.reserve(50);

    // A naive approach on chunk loading:
    // From the player position, find the chunk that is to be loaded.
    // If player moves to a new *chunk*, unload (not sure if that means clear memory), or dont display it and load
    // (allocate memory?) to the new one.
    // For this, number of chunks is not fixed, but variable.

    // Create the resources required for rendering.

    // Vertex buffer setup.
    struct VertexData
    {
        DirectX::XMFLOAT3 position{};
        DirectX::XMFLOAT3 color{};
    };

    // Note : This value is fixed to 1.0 to make sure calculations are as simple as possible.
    static constexpr float voxel_cube_dimension = 1.0f;

    static constexpr u32 number_of_chunks_in_each_dimension = 64;

    // Resources for the *debug draw*, a wireframe rendering to visualize all the chunks.

    // Index buffer setup.
    Buffer debug_grid_index_buffer{};
    Buffer debug_grid_vertex_buffer{};
    D3D12_INDEX_BUFFER_VIEW debug_grid_index_buffer_view{};
    D3D12_VERTEX_BUFFER_VIEW debug_grid_vertex_buffer_view{};

    {
        constexpr u16 index_buffer_data[36] = {0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 4, 5, 1, 4, 1, 0,
                                               3, 2, 6, 3, 6, 7, 1, 5, 6, 1, 6, 2, 4, 0, 3, 4, 3, 7};
        Renderer::BufferPair debug_grid_index_buffer_pair =
            renderer.create_buffer((void *)&index_buffer_data, sizeof(u16) * 36u, BufferTypes::Static);

        intermediate_resources.emplace_back(debug_grid_index_buffer_pair.m_intermediate_buffer.m_buffer);
        debug_grid_index_buffer = debug_grid_index_buffer_pair.m_buffer;

        // Create the index buffer view.
        debug_grid_index_buffer_view = {

            .BufferLocation = debug_grid_index_buffer.m_buffer->GetGPUVirtualAddress(),
            .SizeInBytes = sizeof(u16) * 36u,
            .Format = DXGI_FORMAT_R16_UINT,
        };

        // Vertex buffer setup.
        constexpr VertexData vertex_buffer_data[8] = {
            VertexData{.position = DirectX::XMFLOAT3(-1.0f, -1.0f, -1.0f),
                       .color = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f)}, // 0
            VertexData{.position = DirectX::XMFLOAT3(-1.0f, 1.0f, -1.0f),
                       .color = DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f)}, // 1
            VertexData{.position = DirectX::XMFLOAT3(1.0f, 1.0f, -1.0f),
                       .color = DirectX::XMFLOAT3(1.0f, 1.0f, 0.0f)}, // 2
            VertexData{.position = DirectX::XMFLOAT3(1.0f, -1.0f, -1.0f),
                       .color = DirectX::XMFLOAT3(1.0f, 0.0f, 0.0f)}, // 3
            VertexData{.position = DirectX::XMFLOAT3(-1.0f, -1.0f, 1.0f),
                       .color = DirectX::XMFLOAT3(0.0f, 0.0f, 1.0f)}, // 4
            VertexData{.position = DirectX::XMFLOAT3(-1.0f, 1.0f, 1.0f),
                       .color = DirectX::XMFLOAT3(0.0f, 1.0f, 1.0f)}, // 5
            VertexData{.position = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f),
                       .color = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f)}, // 6
            VertexData{.position = DirectX::XMFLOAT3(1.0f, -1.0f, 1.0f),
                       .color = DirectX::XMFLOAT3(1.0f, 0.0f, 1.0f)} // 7
        };

        Renderer::BufferPair debug_grid_vertex_buffer_pair =
            renderer.create_buffer((void *)&vertex_buffer_data, sizeof(VertexData) * 8u, BufferTypes::Static);

        intermediate_resources.emplace_back(debug_grid_vertex_buffer_pair.m_intermediate_buffer.m_buffer);
        debug_grid_vertex_buffer = debug_grid_vertex_buffer_pair.m_buffer;

        // Create the vertex buffer view.
        debug_grid_vertex_buffer_view = {
            .BufferLocation = debug_grid_vertex_buffer.m_buffer->GetGPUVirtualAddress(),
            .SizeInBytes = sizeof(VertexData) * 8u,
            .StrideInBytes = sizeof(VertexData),
        };
    }

    // Note : Each chunk has its own vertex buffer and transform buffer.
    // The renderer has an array of vertex buffers to simply the process of rendering.
    // For simplicity, all chunks will SHARE the same vertex data. This is because for now the focus is on how to load
    // and unload chunks.

    std::vector<u64> loaded_chunk_indices{};

    // Data shared amongst all chunks.
    u64 chunk_vertices_count{};
    Buffer chunk_vertex_buffer{};
    D3D12_VERTEX_BUFFER_VIEW chunk_vertex_buffer_view{};
    std::vector<VertexData> chunk_vertex_data{};

    // The chunk which is used as template for all chunks (FOR NOW).
    Chunk place_holder_chunk{};

    const DirectX::XMFLOAT3 voxel_color =
        DirectX::XMFLOAT3(rand() / double(RAND_MAX), rand() / double(RAND_MAX), rand() / double(RAND_MAX));

    for (u64 i = 0; i < Chunk::number_of_voxels_per_dimension * Chunk::number_of_voxels_per_dimension *
                            Chunk::number_of_voxels_per_dimension;
         i++)
    {
        // Vertex buffer construction.
        const DirectX::XMUINT3 index = convert_index_to_3d(i, Chunk::number_of_voxels_per_dimension);

        if (place_holder_chunk.m_cubes[i].m_active)
        {
            const DirectX::XMFLOAT3 position_offset =
                DirectX::XMFLOAT3{voxel_cube_dimension * (float)index.x, voxel_cube_dimension * (float)index.y,
                                  voxel_cube_dimension * (float)index.z};
            const float voxel_render_size = voxel_cube_dimension;

            const VertexData v1 = (VertexData{.position = DirectX::XMFLOAT3(position_offset.x - voxel_render_size,
                                                                            position_offset.y - voxel_render_size,
                                                                            position_offset.z - voxel_render_size),
                                              .color = voxel_color});
            const VertexData v2 = (VertexData{.position = DirectX::XMFLOAT3(position_offset.x - voxel_render_size,
                                                                            position_offset.y + voxel_render_size,
                                                                            position_offset.z - voxel_render_size),
                                              .color = voxel_color});
            const VertexData v3 = (VertexData{.position = DirectX::XMFLOAT3(position_offset.x + voxel_render_size,
                                                                            position_offset.y + voxel_render_size,
                                                                            position_offset.z - voxel_render_size),
                                              .color = voxel_color});
            const VertexData v4 = (VertexData{.position = DirectX::XMFLOAT3(position_offset.x + voxel_render_size,
                                                                            position_offset.y - voxel_render_size,
                                                                            position_offset.z - voxel_render_size),
                                              .color = voxel_color});
            const VertexData v5 = (VertexData{.position = DirectX::XMFLOAT3(position_offset.x - voxel_render_size,
                                                                            position_offset.y - voxel_render_size,
                                                                            position_offset.z + voxel_render_size),
                                              .color = voxel_color});
            const VertexData v6 = (VertexData{.position = DirectX::XMFLOAT3(position_offset.x - voxel_render_size,
                                                                            position_offset.y + voxel_render_size,
                                                                            position_offset.z + voxel_render_size),
                                              .color = voxel_color});
            const VertexData v7 = (VertexData{.position = DirectX::XMFLOAT3(position_offset.x + voxel_render_size,
                                                                            position_offset.y + voxel_render_size,
                                                                            position_offset.z + voxel_render_size),
                                              .color = voxel_color});
            const VertexData v8 = (VertexData{.position = DirectX::XMFLOAT3(position_offset.x + voxel_render_size,
                                                                            position_offset.y - voxel_render_size,
                                                                            position_offset.z + voxel_render_size),
                                              .color = voxel_color});

            chunk_vertex_data.reserve(chunk_vertex_data.size() + 36);

            chunk_vertex_data.emplace_back(v1);
            chunk_vertex_data.emplace_back(v2);
            chunk_vertex_data.emplace_back(v3);

            chunk_vertex_data.emplace_back(v1);
            chunk_vertex_data.emplace_back(v3);
            chunk_vertex_data.emplace_back(v4);

            chunk_vertex_data.emplace_back(v5);
            chunk_vertex_data.emplace_back(v7);
            chunk_vertex_data.emplace_back(v6);

            chunk_vertex_data.emplace_back(v5);
            chunk_vertex_data.emplace_back(v8);
            chunk_vertex_data.emplace_back(v7);

            chunk_vertex_data.emplace_back(v5);
            chunk_vertex_data.emplace_back(v6);
            chunk_vertex_data.emplace_back(v2);

            chunk_vertex_data.emplace_back(v5);
            chunk_vertex_data.emplace_back(v2);
            chunk_vertex_data.emplace_back(v1);

            chunk_vertex_data.emplace_back(v4);
            chunk_vertex_data.emplace_back(v3);
            chunk_vertex_data.emplace_back(v7);

            chunk_vertex_data.emplace_back(v4);
            chunk_vertex_data.emplace_back(v7);
            chunk_vertex_data.emplace_back(v8);

            chunk_vertex_data.emplace_back(v2);
            chunk_vertex_data.emplace_back(v6);
            chunk_vertex_data.emplace_back(v7);

            chunk_vertex_data.emplace_back(v2);
            chunk_vertex_data.emplace_back(v7);
            chunk_vertex_data.emplace_back(v3);

            chunk_vertex_data.emplace_back(v5);
            chunk_vertex_data.emplace_back(v1);
            chunk_vertex_data.emplace_back(v4);

            chunk_vertex_data.emplace_back(v5);
            chunk_vertex_data.emplace_back(v4);
            chunk_vertex_data.emplace_back(v8);
        }
    };

    Renderer::BufferPair vertex_buffer_pair = renderer.create_buffer(
        (void *)chunk_vertex_data.data(), sizeof(VertexData) * chunk_vertex_data.size(), BufferTypes::Static);

    intermediate_resources.emplace_back(vertex_buffer_pair.m_intermediate_buffer.m_buffer);
    chunk_vertex_buffer = vertex_buffer_pair.m_buffer;

    // Create the vertex buffer view.
    chunk_vertex_buffer_view = {
        .BufferLocation = chunk_vertex_buffer.m_buffer->GetGPUVirtualAddress(),
        .SizeInBytes = (u32)(sizeof(VertexData) * chunk_vertex_data.size()),
        .StrideInBytes = sizeof(VertexData),
    };

    chunk_vertices_count = chunk_vertex_data.size();

    // Create a 'scene' constant buffer.
    struct alignas(256) SceneConstantBuffer
    {
        DirectX::XMMATRIX view_projection_matrix{};
    };

    Renderer::BufferPair scene_constant_buffer_pair =
        renderer.create_buffer(nullptr, sizeof(SceneConstantBuffer), BufferTypes::Dynamic);
    Buffer scene_constant_buffer = scene_constant_buffer_pair.m_buffer;
    u8 *scene_constant_buffer_ptr = scene_constant_buffer_pair.m_buffer_ptr;

    // Create the scene constant buffer descriptor.
    const D3D12_CPU_DESCRIPTOR_HANDLE scene_constant_buffer_descriptor_handle =
        renderer.create_constant_buffer_view(scene_constant_buffer, sizeof(SceneConstantBuffer));
    (void)scene_constant_buffer_descriptor_handle;

    // Create a 'chunk' constant buffer where per-chunk data is stored.
    struct alignas(256) ChunkConstantBuffer
    {
        DirectX::XMMATRIX transform_buffer{};
    };

    Buffer chunk_constant_buffer;
    u8 *chunk_constant_buffer_ptr;

    Renderer::BufferPair chunk_constant_buffers_pair =
        renderer.create_buffer(nullptr, sizeof(ChunkConstantBuffer), BufferTypes::Dynamic);

    chunk_constant_buffer = chunk_constant_buffers_pair.m_buffer;
    chunk_constant_buffer_ptr = chunk_constant_buffers_pair.m_buffer_ptr;

    renderer.create_constant_buffer_view(chunk_constant_buffer, sizeof(ChunkConstantBuffer));

    // Create the depth stencil buffer.
    // This process is not abstracted because depth resource will not be created multiple times.
    // If in future this happens, a create_xyz function will be created in renderer.
    Microsoft::WRL::ComPtr<ID3D12Resource> depth_resource{};
    D3D12_CPU_DESCRIPTOR_HANDLE dsv_cpu_handle = renderer.m_dsv_descriptor_heap.m_current_cpu_descriptor_handle;
    renderer.m_dsv_descriptor_heap.offset_current_descriptor_handles();
    {
        const D3D12_RESOURCE_DESC ds_resource_desc = {
            .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
            .Alignment = 0u,
            .Width = window.m_width,
            .Height = window.m_height,
            .DepthOrArraySize = 1u,
            .MipLevels = 1u,
            .Format = DXGI_FORMAT_D32_FLOAT,
            .SampleDesc =
                {
                    1u,
                    0u,
                },
            .Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
            .Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
        };

        const D3D12_HEAP_PROPERTIES default_heap_properties = {
            .Type = D3D12_HEAP_TYPE_DEFAULT,
            .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
            .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
            .CreationNodeMask = 0u,
            .VisibleNodeMask = 0u,
        };

        const D3D12_CLEAR_VALUE optimized_ds_clear_value = {
            .Format = DXGI_FORMAT_D32_FLOAT,
            .DepthStencil =
                {
                    .Depth = 1.0f,
                },
        };

        throw_if_failed(renderer.m_device->CreateCommittedResource(
            &default_heap_properties, D3D12_HEAP_FLAG_NONE, &ds_resource_desc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &optimized_ds_clear_value, IID_PPV_ARGS(&depth_resource)));

        const D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {
            .Format = DXGI_FORMAT_D32_FLOAT,
            .ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D,
            .Flags = D3D12_DSV_FLAG_NONE,
            .Texture2D =
                {

                    .MipSlice = 0u,
                },
        };

        renderer.m_device->CreateDepthStencilView(depth_resource.Get(), &dsv_desc, dsv_cpu_handle);
    }

    // There are 2 root signatures : one for the debug grid draw, and one for chunks.
    Microsoft::WRL::ComPtr<ID3DBlob> chunks_root_signature_blob{};
    Microsoft::WRL::ComPtr<ID3D12RootSignature> chunks_root_signature{};

    D3D12_ROOT_PARAMETER1 chunks_root_parameters[2] = {
        // For scene constant buffer.
        D3D12_ROOT_PARAMETER1{
            .ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV,
            .Descriptor =
                D3D12_ROOT_DESCRIPTOR1{
                    .ShaderRegister = 0u,
                    .RegisterSpace = 0u,
                    .Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE,
                },
            .ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX,
        },
        // For chunk constant buffer.
        D3D12_ROOT_PARAMETER1{
            .ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV,
            .Descriptor =
                D3D12_ROOT_DESCRIPTOR1{
                    .ShaderRegister = 1u,
                    .RegisterSpace = 0u,
                    .Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE,
                },
            .ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX,
        },
    };

    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC chunks_root_signature_desc = {
        .Version = D3D_ROOT_SIGNATURE_VERSION_1_1,
        .Desc_1_1 =
            {
                .NumParameters = 2u,
                .pParameters = chunks_root_parameters,
                .NumStaticSamplers = 0u,
                .pStaticSamplers = nullptr,
                .Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT,
            },
    };
    // Serialize root signature.
    throw_if_failed(
        D3D12SerializeVersionedRootSignature(&chunks_root_signature_desc, &chunks_root_signature_blob, nullptr));
    throw_if_failed(renderer.m_device->CreateRootSignature(0u, chunks_root_signature_blob->GetBufferPointer(),
                                                           chunks_root_signature_blob->GetBufferSize(),
                                                           IID_PPV_ARGS(&chunks_root_signature)));

    Microsoft::WRL::ComPtr<ID3DBlob> debug_grid_root_signature_blob{};
    Microsoft::WRL::ComPtr<ID3D12RootSignature> debug_grid_root_signature{};

    D3D12_ROOT_PARAMETER1 debug_grid_root_parameters[2] = {
        // For scene constant buffer.
        D3D12_ROOT_PARAMETER1{
            .ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV,
            .Descriptor =
                D3D12_ROOT_DESCRIPTOR1{
                    .ShaderRegister = 0u,
                    .RegisterSpace = 0u,
                    .Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE,
                },
            .ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX,
        },
        // For instance transform buffer (inline root constant) of a float4x4.
        D3D12_ROOT_PARAMETER1{
            .ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
            .Constants =
                D3D12_ROOT_CONSTANTS{
                    .ShaderRegister = 1u,
                    .RegisterSpace = 0u,
                    .Num32BitValues = 16u,
                },
            .ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX,
        },
    };

    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC debug_grid_root_signature_desc = {
        .Version = D3D_ROOT_SIGNATURE_VERSION_1_1,
        .Desc_1_1 =
            {
                .NumParameters = 2u,
                .pParameters = debug_grid_root_parameters,
                .NumStaticSamplers = 0u,
                .pStaticSamplers = nullptr,
                .Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT,
            },
    };
    // Serialize root signature.
    throw_if_failed(D3D12SerializeVersionedRootSignature(&debug_grid_root_signature_desc,
                                                         &debug_grid_root_signature_blob, nullptr));
    throw_if_failed(renderer.m_device->CreateRootSignature(0u, debug_grid_root_signature_blob->GetBufferPointer(),
                                                           debug_grid_root_signature_blob->GetBufferSize(),
                                                           IID_PPV_ARGS(&debug_grid_root_signature)));

    // Compile the vertex and pixel shader.
    Microsoft::WRL::ComPtr<IDxcBlob> vertex_shader_blob =
        ShaderCompiler::compile(L"shaders/shader.hlsl", L"vs_main", L"vs_6_0");

    Microsoft::WRL::ComPtr<IDxcBlob> pixel_shader_blob =
        ShaderCompiler::compile(L"shaders/shader.hlsl", L"ps_main", L"ps_6_0");

    // Create the graphics pso.
    const D3D12_INPUT_ELEMENT_DESC input_element_descs[2] = {
        D3D12_INPUT_ELEMENT_DESC{
            .SemanticName = "POSITION",
            .SemanticIndex = 0u,
            .Format = DXGI_FORMAT_R32G32B32_FLOAT,
            .InputSlot = 0u,
            .AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT,
            .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            .InstanceDataStepRate = 0u,
        },
        D3D12_INPUT_ELEMENT_DESC{
            .SemanticName = "COLOR",
            .SemanticIndex = 0u,
            .Format = DXGI_FORMAT_R32G32B32_FLOAT,
            .InputSlot = 0u,
            .AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT,
            .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            .InstanceDataStepRate = 0u,
        },
    };

    Microsoft::WRL::ComPtr<ID3D12PipelineState> chunks_pso{};
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC chunks_graphics_pso_desc = {
        .pRootSignature = chunks_root_signature.Get(),
        .VS =
            {
                .pShaderBytecode = vertex_shader_blob->GetBufferPointer(),
                .BytecodeLength = vertex_shader_blob->GetBufferSize(),
            },
        .PS =
            {
                .pShaderBytecode = pixel_shader_blob->GetBufferPointer(),
                .BytecodeLength = pixel_shader_blob->GetBufferSize(),
            },
        .BlendState =
            {
                .AlphaToCoverageEnable = FALSE,
                .IndependentBlendEnable = FALSE,
                .RenderTarget =
                    {
                        D3D12_RENDER_TARGET_BLEND_DESC{
                            .RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL,
                        },
                    },
            },
        .SampleMask = 0xffff'ffff,
        .RasterizerState =
            {
                .FillMode = D3D12_FILL_MODE_SOLID,
                .CullMode = D3D12_CULL_MODE_BACK,
                .FrontCounterClockwise = FALSE,
            },
        .DepthStencilState =
            {
                .DepthEnable = TRUE,
                .DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL,
                .DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL,
                .StencilEnable = FALSE,
            },
        .InputLayout =
            {
                .pInputElementDescs = input_element_descs,
                .NumElements = 2u,
            },
        .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
        .NumRenderTargets = 1u,
        .RTVFormats =
            {
                DXGI_FORMAT_R10G10B10A2_UNORM,
            },
        .DSVFormat = DXGI_FORMAT_D32_FLOAT,
        .SampleDesc =
            {
                1u,
                0u,
            },
        .NodeMask = 0u,
    };
    throw_if_failed(
        renderer.m_device->CreateGraphicsPipelineState(&chunks_graphics_pso_desc, IID_PPV_ARGS(&chunks_pso)));

    // Compile the vertex and pixel shader.
    Microsoft::WRL::ComPtr<IDxcBlob> debug_grid_vertex_shader_blob =
        ShaderCompiler::compile(L"shaders/debug_grid_shader.hlsl", L"vs_main", L"vs_6_0");

    Microsoft::WRL::ComPtr<IDxcBlob> debug_grid_pixel_shader_blob =
        ShaderCompiler::compile(L"shaders/debug_grid_shader.hlsl", L"ps_main", L"ps_6_0");

    Microsoft::WRL::ComPtr<ID3D12PipelineState> debug_grid_pso{};
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC debug_grid_graphics_pso_desc = {
        .pRootSignature = debug_grid_root_signature.Get(),
        .VS =
            {
                .pShaderBytecode = debug_grid_vertex_shader_blob->GetBufferPointer(),
                .BytecodeLength = debug_grid_vertex_shader_blob->GetBufferSize(),
            },
        .PS =
            {
                .pShaderBytecode = debug_grid_pixel_shader_blob->GetBufferPointer(),
                .BytecodeLength = debug_grid_pixel_shader_blob->GetBufferSize(),
            },
        .BlendState =
            {
                .AlphaToCoverageEnable = FALSE,
                .IndependentBlendEnable = FALSE,
                .RenderTarget =
                    {
                        D3D12_RENDER_TARGET_BLEND_DESC{
                            .RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL,
                        },
                    },
            },
        .SampleMask = 0xffff'ffff,
        .RasterizerState =
            {
                .FillMode = D3D12_FILL_MODE_WIREFRAME,
                .CullMode = D3D12_CULL_MODE_BACK,
                .FrontCounterClockwise = FALSE,
            },
        .DepthStencilState =
            {
                .DepthEnable = TRUE,
                .DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL,
                .DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL,
                .StencilEnable = FALSE,
            },
        .InputLayout =
            {
                .pInputElementDescs = input_element_descs,
                .NumElements = 2u,
            },
        .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
        .NumRenderTargets = 1u,
        .RTVFormats =
            {
                DXGI_FORMAT_R10G10B10A2_UNORM,
            },
        .DSVFormat = DXGI_FORMAT_D32_FLOAT,
        .SampleDesc =
            {
                1u,
                0u,
            },
        .NodeMask = 0u,
    };
    throw_if_failed(
        renderer.m_device->CreateGraphicsPipelineState(&debug_grid_graphics_pso_desc, IID_PPV_ARGS(&debug_grid_pso)));

    // Create viewport and scissor.
    const D3D12_VIEWPORT viewport = {
        .TopLeftX = 0.0f,
        .TopLeftY = 0.0f,
        .Width = (float)window.m_width,
        .Height = (float)window.m_height,
        .MinDepth = 0.0f,
        .MaxDepth = 1.0f,
    };

    // The default config is used if we want to mask the entire viewport for drawing.
    const D3D12_RECT scissor_rect = {
        .left = 0u,
        .top = 0u,
        .right = LONG_MAX,
        .bottom = LONG_MAX,
    };

    // Flush the gpu.
    renderer.execute_command_list();
    renderer.flush_gpu();

    intermediate_resources.clear();

    u64 frame_count = 0u;

    bool quit = false;

    // Timing related data.

    // Get the performance counter frequency (in seconds).
    LARGE_INTEGER performance_frequency = {};
    QueryPerformanceFrequency(&performance_frequency);

    const float seconds_per_count = 1.0f / (float)performance_frequency.QuadPart;

    LARGE_INTEGER frame_start_time = {};
    LARGE_INTEGER frame_end_time = {};

    float delta_time = 0.0;

    Camera camera{};
    camera.m_camera_rotation_speed = 1.0f;
    while (!quit)
    {
        QueryPerformanceCounter(&frame_start_time);

        MSG message = {};
        if (PeekMessageA(&message, NULL, 0u, 0u, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }

        if (message.message == WM_QUIT)
        {
            quit = true;
        }

        const float aspect_ratio = (float)window.m_width / (float)window.m_height;
        const float vertical_fov = DirectX::XMConvertToRadians(45.0f);
        const DirectX::XMMATRIX projection_matrix =
            DirectX::XMMatrixPerspectiveFovLH(vertical_fov, aspect_ratio, 0.1, 100.0f);

        const DirectX::XMMATRIX view_projection_matrix =
            camera.update_and_get_view_matrix(delta_time) * projection_matrix;

        // Now, for finding *which chunk* the camera is in.
        DirectX::XMFLOAT3 camera_position{};
        DirectX::XMStoreFloat3(&camera_position, camera.m_camera_position);

        const DirectX::XMINT3 current_chunk_3d_index = {
            (i32)(floor(fabs(camera_position.x) / i32(Chunk::number_of_voxels_per_dimension))),
            (i32)(floor(fabs(camera_position.y) / i32(Chunk::number_of_voxels_per_dimension))),
            (i32)(floor(fabs(camera_position.z) / i32(Chunk::number_of_voxels_per_dimension))),
        };

        // Update cbuffers.
        SceneConstantBuffer buffer = {
            .view_projection_matrix = view_projection_matrix,
        };
        memcpy(scene_constant_buffer_ptr, &buffer, sizeof(SceneConstantBuffer));

        // Main render loop.

        // First, reset the command allocator and command list.
        u8 &swapchain_backbuffer_index = renderer.m_swapchain_backbuffer_index;
        ID3D12GraphicsCommandList *command_list = renderer.m_command_list.Get();

        throw_if_failed(renderer.m_direct_command_allocators[swapchain_backbuffer_index]->Reset());
        throw_if_failed(
            command_list->Reset(renderer.m_direct_command_allocators[swapchain_backbuffer_index].Get(), nullptr));

        // Get the backbuffer rtv cpu descriptor handle, transition the back buffer to render target, and clear rt.
        D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle =
            renderer.m_swapchain_backbuffer_cpu_descriptor_handles[swapchain_backbuffer_index];

        ID3D12Resource *const current_backbuffer_resource =
            renderer.m_swapchain_backbuffer_resources[swapchain_backbuffer_index].Get();

        // Transition the backbuffer from presentation mode to render target mode.
        const D3D12_RESOURCE_BARRIER presentation_to_render_target_barrier = {
            .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
            .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
            .Transition =
                {
                    .pResource = current_backbuffer_resource,
                    .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                    .StateBefore = D3D12_RESOURCE_STATE_PRESENT,
                    .StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET,
                },
        };

        command_list->ResourceBarrier(1u, &presentation_to_render_target_barrier);

        // Now, clear the RTV and DSV.
        const float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        command_list->ClearRenderTargetView(rtv_handle, clear_color, 0u, nullptr);
        command_list->ClearDepthStencilView(dsv_cpu_handle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0u, 0u, nullptr);

        // Set viewport.
        command_list->RSSetViewports(1u, &viewport);
        command_list->RSSetScissorRects(1u, &scissor_rect);

        command_list->OMSetRenderTargets(
            1u, &renderer.m_swapchain_backbuffer_cpu_descriptor_handles[swapchain_backbuffer_index], FALSE,
            &dsv_cpu_handle);

        ID3D12DescriptorHeap *const shader_visible_descriptor_heaps = {
            renderer.m_cbv_srv_uav_descriptor_heap.m_descriptor_heap.Get()};
        command_list->SetDescriptorHeaps(1u, &shader_visible_descriptor_heaps);

        // Render the debug grid.
        {
            command_list->SetGraphicsRootSignature(debug_grid_root_signature.Get());
            command_list->SetPipelineState(debug_grid_pso.Get());

            command_list->SetGraphicsRootConstantBufferView(0u, scene_constant_buffer.m_buffer->GetGPUVirtualAddress());
            command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            command_list->IASetVertexBuffers(0u, 1u, &debug_grid_vertex_buffer_view);
            command_list->IASetIndexBuffer(&debug_grid_index_buffer_view);

            for (u64 grid_index = 0; grid_index < number_of_chunks_in_each_dimension; grid_index++)
            {
                const DirectX::XMUINT3 grid_offset =
                    convert_index_to_3d(grid_index, number_of_chunks_in_each_dimension);
                (void)grid_offset;

                const DirectX::XMMATRIX transform_matrix = DirectX::XMMatrixScaling(
                    (float)Chunk::number_of_voxels_per_dimension, (float)Chunk::number_of_voxels_per_dimension,
                    (float)Chunk::number_of_voxels_per_dimension);
                /*
                    DirectX::XMMatrixTranslation(grid_offset.x * Chunk::number_of_voxels_per_dimension,
                                                 grid_offset.y * Chunk::number_of_voxels_per_dimension,
                                                 grid_offset.z * Chunk::number_of_voxels_per_dimension);
                                                 */
                ;

                command_list->SetGraphicsRoot32BitConstants(1u, 16u, &transform_matrix, 0u);

                command_list->DrawIndexedInstanced(36u, 1u, 0u, 0u, 0u);
            }
        }

        // Set the index buffer, pso and all config settings for rendering.
        command_list->SetGraphicsRootSignature(chunks_root_signature.Get());
        command_list->SetPipelineState(chunks_pso.Get());

        command_list->SetGraphicsRootConstantBufferView(0u, scene_constant_buffer.m_buffer->GetGPUVirtualAddress());

        command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // Render all chunks.
        // Check if there is a chunk loaded at current position.
        const u64 current_chunk_index = convert_index_to_1d(current_chunk_3d_index, number_of_chunks_in_each_dimension,
                                                            number_of_chunks_in_each_dimension / 2u);

        // Check if current chunk is in memory.
        bool chunk_already_in_memory = false;
        for (const auto &chunk_index : loaded_chunk_indices)
        {
            if (chunk_index == current_chunk_index)
            {
                chunk_already_in_memory = true;
            }
        }

        if (!chunk_already_in_memory)
        {
            // Create chunk and add to vector.
            loaded_chunk_indices.push_back(current_chunk_index);
        }

        for (const auto &chunk_index : loaded_chunk_indices)
        {
            const DirectX::XMUINT3 _chunk_index_3d =
                convert_index_to_3d(chunk_index, number_of_chunks_in_each_dimension);
            const DirectX::XMINT3 chunk_index_3d = {
                (i32)_chunk_index_3d.x - i32(number_of_chunks_in_each_dimension / 2u),
                (i32)_chunk_index_3d.y - i32(number_of_chunks_in_each_dimension / 2u),
                (i32)_chunk_index_3d.z - i32(number_of_chunks_in_each_dimension / 2u)};

            ChunkConstantBuffer chunk_constant_buffer_data = {
                .transform_buffer =
                    DirectX::XMMatrixTranslation(chunk_index_3d.x * (i32)Chunk::number_of_voxels_per_dimension,
                                                 (chunk_index_3d.y - 1) * (i32)Chunk::number_of_voxels_per_dimension,
                                                 (chunk_index_3d.z * (i32)Chunk::number_of_voxels_per_dimension)),
            };
            memcpy(chunk_constant_buffer_ptr, &chunk_constant_buffer_data, sizeof(ChunkConstantBuffer));

            command_list->SetGraphicsRootConstantBufferView(1u, chunk_constant_buffer.m_buffer->GetGPUVirtualAddress());
            command_list->IASetVertexBuffers(0u, 1u, &chunk_vertex_buffer_view);
            command_list->DrawInstanced(chunk_vertices_count, 1u, 0u, 0u);
        }

        // Now, transition back to presentation mode.
        const D3D12_RESOURCE_BARRIER render_target_to_presentation_barrier = {
            .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
            .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
            .Transition =
                {
                    .pResource = current_backbuffer_resource,
                    .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                    .StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET,
                    .StateAfter = D3D12_RESOURCE_STATE_PRESENT,
                },
        };

        command_list->ResourceBarrier(1u, &render_target_to_presentation_barrier);

        // Submit command list to queue for execution.
        renderer.execute_command_list();

        // Now, present the rendertarget and signal command queue.
        throw_if_failed(renderer.m_swapchain->Present(1u, 0u));
        renderer.signal_fence();

        renderer.m_swapchain_backbuffer_index = renderer.m_swapchain->GetCurrentBackBufferIndex();

        // Wait for the previous frame (that is presenting to swpachain_backbuffer_index) to complete execution.
        renderer.wait_for_fence_value_at_index(renderer.m_swapchain_backbuffer_index);

        ++frame_count;

        QueryPerformanceCounter(&frame_end_time);

        delta_time = (frame_end_time.QuadPart - frame_start_time.QuadPart) * seconds_per_count;
    }

    printf("Frames renderer :: %u", (u32)frame_count);

    renderer.flush_gpu();

    return 0;
}