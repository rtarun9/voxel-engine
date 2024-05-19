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
// If the grid can have -ve values (such as the chunks in the grid), the calling code has to subtract N / 2 from the 3d
// index.
static inline DirectX::XMUINT3 convert_index_to_3d(const u64 index, const u32 N)
{
    // Note that index = x + y * N + z * N * N;
    const u32 z = index / (N * N);
    const u32 index_2d = index - z * N * N;
    const u32 y = index_2d / N;
    const u32 x = index_2d % N;

    return DirectX::XMUINT3{x, y, z};
}

static inline u64 convert_index_to_1d(const DirectX::XMINT3 index, const u32 N, const u32 bias_value = 0u)
{
    return ((i32)bias_value + index.x) + ((i32)bias_value + index.y) * N + ((i32)bias_value + index.z) * N * N;
}

int main()
{
    const Window window{};
    Renderer renderer(window.m_handle, window.m_width, window.m_height);

    // A vector of intermediate resources (required since intermediate buffers need to be in memory until the copy
    // resource and other functions are executed).
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> intermediate_resources{};
    intermediate_resources.reserve(128);

    // Create the resources required for rendering.

    // Vertex buffer setup.
    struct VertexData
    {
        DirectX::XMFLOAT3 position{};
        DirectX::XMFLOAT3 color{};
    };

    static constexpr float voxel_cube_dimension = 16.0f;

    // If a chunk that is loaded is 'chunk_render_distance' units away from the player in any direction, it is moved
    // into the unloaded_chunks vector.
    static constexpr u64 chunk_render_distance = 8u;

    static constexpr u32 number_of_chunks_in_each_dimension = 16u;
    static constexpr u64 number_of_chunks =
        number_of_chunks_in_each_dimension * number_of_chunks_in_each_dimension * number_of_chunks_in_each_dimension;

    // Resources for the *debug draw*, a wireframe rendering to visualize all the chunks.
    // the debug draw will use instanced rendering. The 3D offset is determined in the shader itself.

    // Index buffer setup.
    Buffer debug_grid_index_buffer{};
    Buffer debug_grid_vertex_buffer{};
    D3D12_INDEX_BUFFER_VIEW debug_grid_index_buffer_view{};
    D3D12_VERTEX_BUFFER_VIEW debug_grid_vertex_buffer_view{};

    constexpr u16 index_buffer_data[24] = {0, 1, 1, 2, 2, 3, 3, 0, 4, 5, 5, 6, 6, 7, 7, 4, 0, 4, 1, 5, 2, 6, 3, 7};

    // Vertex buffer setup.
    // In this engine, the *voxel* is a point sample and represents(sort of) the FRONT LOWER LEFT point in the actual
    // CUBE.
    constexpr VertexData vertex_buffer_data[8] = {
        VertexData{
            .position = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f),
            .color = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f),
        }, // 0
        VertexData{
            .position = DirectX::XMFLOAT3(0.0f, voxel_cube_dimension, 0.0f),
            .color = DirectX::XMFLOAT3(0.0f, 0.5f, 0.0f),
        }, // 1
        VertexData{
            .position = DirectX::XMFLOAT3(voxel_cube_dimension, voxel_cube_dimension, 0.0f),
            .color = DirectX::XMFLOAT3(0.5f, 0.5f, 0.0f),
        }, // 2
        VertexData{
            .position = DirectX::XMFLOAT3(voxel_cube_dimension, 0.0f, 0.0f),
            .color = DirectX::XMFLOAT3(0.5f, 0.0f, 0.0f),
        }, // 3
        VertexData{
            .position = DirectX::XMFLOAT3(0.0f, 0.0f, voxel_cube_dimension),
            .color = DirectX::XMFLOAT3(0.0f, 0.0f, 0.5f),
        }, // 4
        VertexData{
            .position = DirectX::XMFLOAT3(0.0f, voxel_cube_dimension, voxel_cube_dimension),
            .color = DirectX::XMFLOAT3(0.0f, 0.5f, 0.5f),
        }, // 5
        VertexData{
            .position = DirectX::XMFLOAT3(voxel_cube_dimension, voxel_cube_dimension, voxel_cube_dimension),
            .color = DirectX::XMFLOAT3(0.5f, 0.5f, 0.5f),
        }, // 6
        VertexData{
            .position = DirectX::XMFLOAT3(voxel_cube_dimension, 0.0f, voxel_cube_dimension),
            .color = DirectX::XMFLOAT3(0.5f, 0.0f, 0.5f),
        } // 7
    };

    {
        Renderer::BufferPair debug_grid_index_buffer_pair =
            renderer.create_buffer((void *)&index_buffer_data, sizeof(u16) * 24u, BufferTypes::Static);

        intermediate_resources.emplace_back(debug_grid_index_buffer_pair.m_intermediate_buffer.m_buffer);
        debug_grid_index_buffer = debug_grid_index_buffer_pair.m_buffer;

        // Create the index buffer view.
        debug_grid_index_buffer_view = {

            .BufferLocation = debug_grid_index_buffer.m_buffer->GetGPUVirtualAddress(),
            .SizeInBytes = sizeof(u16) * 24u,
            .Format = DXGI_FORMAT_R16_UINT,
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

    // Loaded chunks : In memory, and can be rendered.
    std::vector<Chunk> loaded_chunks{};

    // Chunks which are to be created (memory must be allocated).
    std::vector<Chunk> chunks_to_load{};

    // Chunks which are in memory, but are no longer rendered.
    std::vector<Chunk> unloaded_chunks{};

    // Data for chunks.
    // In the hashmap, the key is the chunk index (each chunks knows this information), and the value is as the hahsmap
    // name.
    std::unordered_map<u64, u64> chunk_vertices_counts{};
    std::unordered_map<u64, Buffer> chunk_vertex_buffers{};
    std::unordered_map<u64, std::vector<VertexData>> chunk_vertex_datas{};
    std::unordered_map<u64, Cube *> chunk_cubes{};

    // This function adds the chunk to the chunks_to_load vector.
    // Will not create a chunk if index is already in loaded_chunks vector.
    const auto create_chunk = [&](const auto chunk_index) {
        for (const auto &loaded_chunk : loaded_chunks)
        {
            if (loaded_chunk.m_chunk_index == chunk_index)
            {
                return;
            }
        }
        Chunk chunk{};

        chunk_vertex_datas[chunk_index] = std::vector<VertexData>{};
        chunk_cubes[chunk_index] = new Cube[Chunk::number_of_voxels];

        const DirectX::XMFLOAT3 voxel_color =
            DirectX::XMFLOAT3(rand() / double(RAND_MAX), rand() / double(RAND_MAX), rand() / double(RAND_MAX));

        for (u64 i = 0; i < Chunk::number_of_voxels; i++)
        {
            chunk.m_chunk_index = chunk_index;

            chunk_vertex_datas[chunk_index].reserve(chunk_vertex_datas.size() + 36u);

            // Vertex buffer construction.
            const DirectX::XMUINT3 index = convert_index_to_3d(i, Chunk::number_of_voxels_per_dimension);

            // If a voxel is being surrounded on all sides by another voxel, dont render it at all.
            const auto check_if_voxel_is_covered_on_all_sides = [&](const DirectX::XMUINT3 voxel_index) -> bool {
                // If there is a axis where the value is 0 or number_of_chunks, (i.e this voxel is on edge of the cube,
                // render it.
                if (voxel_index.x == 0 || voxel_index.y == 0 || voxel_index.z == 0 ||
                    voxel_index.x == Chunk::number_of_voxels_per_dimension - 1 ||
                    voxel_index.y == Chunk::number_of_voxels_per_dimension - 1 ||
                    voxel_index.z == Chunk::number_of_voxels_per_dimension - 1)
                {
                    return true;
                }
                else
                {
                    // If all 8 neighbouring voxels are being rendered, dont render this.
                    u8 number_of_neighbouring_active_voxels = 0;
                    for (i8 i = -1; i <= 1; i++)
                    {
                        for (i8 j = -1; j <= 1; j++)
                        {
                            for (i8 k = -1; k <= 1; k++)
                            {
                                Cube neighbouring_voxel = chunk_cubes[chunk_index][convert_index_to_1d(
                                    DirectX::XMINT3{(i32)voxel_index.x + i, (i32)voxel_index.y + j,
                                                    (i32)voxel_index.z + k},
                                    Chunk::number_of_voxels_per_dimension)];

                                if (neighbouring_voxel.m_active)
                                {
                                    number_of_neighbouring_active_voxels++;
                                }
                            }
                        }
                    }

                    if (number_of_neighbouring_active_voxels != 8u)
                    {
                        return false;
                    }

                    return true;
                }
            };

            if (chunk_cubes[chunk_index][i].m_active && check_if_voxel_is_covered_on_all_sides(index))
            {
                const DirectX::XMFLOAT3 position_offset =
                    DirectX::XMFLOAT3{(float)index.x, (float)index.y, (float)index.z};

                const VertexData v1 =
                    (VertexData{.position = DirectX::XMFLOAT3(position_offset.x, position_offset.y, position_offset.z),
                                .color = voxel_color});
                const VertexData v2 =
                    (VertexData{.position = DirectX::XMFLOAT3(
                                    position_offset.x, position_offset.y + voxel_cube_dimension, position_offset.z),
                                .color = voxel_color});
                const VertexData v3 = (VertexData{
                    .position = DirectX::XMFLOAT3(position_offset.x + voxel_cube_dimension,
                                                  position_offset.y + voxel_cube_dimension, position_offset.z),
                    .color = voxel_color});
                const VertexData v4 =
                    (VertexData{.position = DirectX::XMFLOAT3(position_offset.x + voxel_cube_dimension,
                                                              position_offset.y, position_offset.z),
                                .color = voxel_color});
                const VertexData v5 =
                    (VertexData{.position = DirectX::XMFLOAT3(position_offset.x, position_offset.y,
                                                              position_offset.z + voxel_cube_dimension),
                                .color = voxel_color});
                const VertexData v6 = (VertexData{
                    .position = DirectX::XMFLOAT3(position_offset.x, position_offset.y + voxel_cube_dimension,
                                                  position_offset.z + voxel_cube_dimension),
                    .color = voxel_color});
                const VertexData v7 =
                    (VertexData{.position = DirectX::XMFLOAT3(position_offset.x + voxel_cube_dimension,
                                                              position_offset.y + voxel_cube_dimension,
                                                              position_offset.z + voxel_cube_dimension),
                                .color = voxel_color});
                const VertexData v8 = (VertexData{
                    .position = DirectX::XMFLOAT3(position_offset.x + voxel_cube_dimension, position_offset.y,
                                                  position_offset.z + voxel_cube_dimension),
                    .color = voxel_color});

                chunk_vertex_datas[chunk_index].emplace_back(v1);
                chunk_vertex_datas[chunk_index].emplace_back(v2);
                chunk_vertex_datas[chunk_index].emplace_back(v3);

                chunk_vertex_datas[chunk_index].emplace_back(v1);
                chunk_vertex_datas[chunk_index].emplace_back(v3);
                chunk_vertex_datas[chunk_index].emplace_back(v4);

                chunk_vertex_datas[chunk_index].emplace_back(v5);
                chunk_vertex_datas[chunk_index].emplace_back(v7);
                chunk_vertex_datas[chunk_index].emplace_back(v6);

                chunk_vertex_datas[chunk_index].emplace_back(v5);
                chunk_vertex_datas[chunk_index].emplace_back(v8);
                chunk_vertex_datas[chunk_index].emplace_back(v7);

                chunk_vertex_datas[chunk_index].emplace_back(v5);
                chunk_vertex_datas[chunk_index].emplace_back(v6);
                chunk_vertex_datas[chunk_index].emplace_back(v2);

                chunk_vertex_datas[chunk_index].emplace_back(v5);
                chunk_vertex_datas[chunk_index].emplace_back(v2);
                chunk_vertex_datas[chunk_index].emplace_back(v1);

                chunk_vertex_datas[chunk_index].emplace_back(v4);
                chunk_vertex_datas[chunk_index].emplace_back(v3);
                chunk_vertex_datas[chunk_index].emplace_back(v7);

                chunk_vertex_datas[chunk_index].emplace_back(v4);
                chunk_vertex_datas[chunk_index].emplace_back(v7);
                chunk_vertex_datas[chunk_index].emplace_back(v8);

                chunk_vertex_datas[chunk_index].emplace_back(v2);
                chunk_vertex_datas[chunk_index].emplace_back(v6);
                chunk_vertex_datas[chunk_index].emplace_back(v7);

                chunk_vertex_datas[chunk_index].emplace_back(v2);
                chunk_vertex_datas[chunk_index].emplace_back(v7);
                chunk_vertex_datas[chunk_index].emplace_back(v3);

                chunk_vertex_datas[chunk_index].emplace_back(v5);
                chunk_vertex_datas[chunk_index].emplace_back(v1);
                chunk_vertex_datas[chunk_index].emplace_back(v4);

                chunk_vertex_datas[chunk_index].emplace_back(v5);
                chunk_vertex_datas[chunk_index].emplace_back(v4);
                chunk_vertex_datas[chunk_index].emplace_back(v8);
            }
        };

        Renderer::BufferPair vertex_buffer_pair =
            renderer.create_buffer((void *)chunk_vertex_datas[chunk_index].data(),
                                   sizeof(VertexData) * chunk_vertex_datas[chunk_index].size(), BufferTypes::Static);

        intermediate_resources.emplace_back(vertex_buffer_pair.m_intermediate_buffer.m_buffer);

        chunk_vertex_buffers[chunk_index] = vertex_buffer_pair.m_buffer;
        chunk_vertices_counts[chunk_index] = (u64)chunk_vertex_datas[chunk_index].size();

        chunks_to_load.push_back(std::move(chunk));
    };

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

    Buffer chunk_constant_buffer{};
    u8 *chunk_constant_buffer_ptr{};
    USE(chunk_constant_buffer_ptr);

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
        // For chunk constant buffer (in future make this a constant buffer, but for testing purposes it is a inline 32
        // bit root constants).
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
        // For instance transform buffer (inline root constant) of a float4x4 for transform data and 3 uint's for
        // instance count and number of chunks per dimensions and number of voxels per dimension.
        D3D12_ROOT_PARAMETER1{
            .ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
            .Constants =
                D3D12_ROOT_CONSTANTS{
                    .ShaderRegister = 1u,
                    .RegisterSpace = 0u,
                    .Num32BitValues = 20u,
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
                .FrontCounterClockwise = TRUE,
            },
        .DepthStencilState =
            {
                .DepthEnable = TRUE,
                .DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL,
                .DepthFunc = D3D12_COMPARISON_FUNC_LESS,
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
                .FrontCounterClockwise = TRUE,
            },
        .DepthStencilState =
            {
                .DepthEnable = TRUE,
                .DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL,
                .DepthFunc = D3D12_COMPARISON_FUNC_LESS,
                .StencilEnable = FALSE,
            },
        .InputLayout =
            {
                .pInputElementDescs = input_element_descs,
                .NumElements = 2u,
            },
        .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE,
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
            DirectX::XMMatrixPerspectiveFovLH(vertical_fov, aspect_ratio, 0.1, 1000.0f);

        const DirectX::XMMATRIX view_projection_matrix =
            camera.update_and_get_view_matrix(delta_time) * projection_matrix;

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
            command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);

            command_list->IASetVertexBuffers(0u, 1u, &debug_grid_vertex_buffer_view);
            command_list->IASetIndexBuffer(&debug_grid_index_buffer_view);

            {
                struct DebugGridRootConstants
                {
                    DirectX::XMMATRIX matrix{};
                    u32 number_of_chunks_per_dimension{};
                    u32 number_of_voxels_per_dimension{};
                };

                DebugGridRootConstants root_constants = {
                    .matrix = DirectX::XMMatrixScaling((float)Chunk::number_of_voxels_per_dimension,
                                                       (float)Chunk::number_of_voxels_per_dimension,
                                                       (float)Chunk::number_of_voxels_per_dimension),
                    .number_of_chunks_per_dimension = number_of_chunks_in_each_dimension,
                    .number_of_voxels_per_dimension = Chunk::number_of_voxels_per_dimension,
                };

                command_list->SetGraphicsRoot32BitConstants(1u, 18u, &root_constants, 0u);

                command_list->DrawIndexedInstanced(24u, number_of_chunks, 0u, 0u, 0u);
            }

            // Set the index buffer, pso and all config settings for rendering.
            command_list->SetGraphicsRootSignature(chunks_root_signature.Get());
            command_list->SetPipelineState(chunks_pso.Get());

            command_list->SetGraphicsRootConstantBufferView(0u, scene_constant_buffer.m_buffer->GetGPUVirtualAddress());

            command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            // From the previous frames chunk to load vector, move thoss chunks to the *loaded chunks* array.
            for (auto &newly_loaded_chunks : chunks_to_load)
            {
                chunk_vertex_datas[newly_loaded_chunks.m_chunk_index].clear();
                loaded_chunks.emplace_back(newly_loaded_chunks);
            }

            chunks_to_load.clear();

            DirectX::XMFLOAT3 camera_position{};
            DirectX::XMStoreFloat3(&camera_position, camera.m_camera_position);

            const DirectX::XMINT3 current_chunk_3d_index = {
                (i32)(floor((camera_position.x) / i32(Chunk::number_of_voxels_per_dimension))),
                (i32)(floor((camera_position.y) / i32(Chunk::number_of_voxels_per_dimension))),
                (i32)(floor((camera_position.z) / i32(Chunk::number_of_voxels_per_dimension))),
            };

            const u64 current_chunk_index = convert_index_to_1d(
                current_chunk_3d_index, number_of_chunks_in_each_dimension, number_of_chunks_in_each_dimension / 2u);

            // Loop through the unloaded chunk vector.
            // and load the chunks that are close enough to the player.

            // note(rtarun9) : In future, convert this code to be more optimized (some sort of spatial data structure).
            for (u32 i = 0; i < unloaded_chunks.size();)
            {
                const DirectX::XMUINT3 _chunk_index_3d =
                    convert_index_to_3d(loaded_chunks[i].m_chunk_index, number_of_chunks_in_each_dimension);

                const DirectX::XMINT3 chunk_index_3d = {
                    (i32)_chunk_index_3d.x - (i32)(number_of_chunks_in_each_dimension / 2u),
                    (i32)_chunk_index_3d.y - (i32)(number_of_chunks_in_each_dimension / 2u),
                    (i32)_chunk_index_3d.z - (i32)(number_of_chunks_in_each_dimension / 2u),
                };
                if ((u64)fabs(chunk_index_3d.x - current_chunk_3d_index.x) <= chunk_render_distance ||
                    (u64)fabs(chunk_index_3d.y - current_chunk_3d_index.y) <= chunk_render_distance ||
                    (u64)fabs(chunk_index_3d.z - current_chunk_3d_index.z) <= chunk_render_distance)
                {
                    loaded_chunks.emplace_back(unloaded_chunks[i]);
                    unloaded_chunks.erase(unloaded_chunks.begin() + i);
                }
                else
                {
                    ++i;
                }
            }

            // Check if current chunk (and chunk_render_distance chunks around it) is in memory.
            // Unoptimized code, focus is on getting things done first and later optimize.
            // Load chunks with same y as current check for now.
            std::vector<u64> chunks_to_check{};
            chunks_to_check.reserve(chunk_render_distance * chunk_render_distance * chunk_render_distance);
            chunks_to_check.emplace_back(current_chunk_index);
            for (i32 x = -(i32)chunk_render_distance; x < (i32)chunk_render_distance; x++)
            {
                for (i32 z = -(i32)chunk_render_distance; z < (i32)chunk_render_distance; z++)
                {
                    if (x != z)
                    {
                        DirectX::XMINT3 chunk_3d_index = {
                            current_chunk_3d_index.x + x,
                            current_chunk_3d_index.y,
                            current_chunk_3d_index.z + z,
                        };

                        chunks_to_check.emplace_back(convert_index_to_1d(chunk_3d_index,
                                                                         number_of_chunks_in_each_dimension,
                                                                         number_of_chunks_in_each_dimension / 2u));
                    }
                }
            }

            // Unoptimized...
            for (const auto &chunk_to_check : chunks_to_check)
            {
                bool chunk_already_in_memory = false;
                for (u64 i = 0; i < loaded_chunks.size(); i++)
                {
                    if (loaded_chunks[i].m_chunk_index == chunk_to_check)
                    {
                        chunk_already_in_memory = true;
                    }
                }

                // FOR DEBUG PURPOSES ONLY.
                // If caps lock key is pressed, do not load the current chunk.
                if (!(GetKeyState(VK_CAPITAL) & 0x0001) && !chunk_already_in_memory)
                {
                    create_chunk(chunk_to_check);
                }
            }

            // Loop through loaded chunks, and whatever is too far, move to the unloaded vector.
            for (u64 i = 0; i < loaded_chunks.size();)
            {
                const DirectX::XMUINT3 _chunk_index_3d =
                    convert_index_to_3d(loaded_chunks[i].m_chunk_index, number_of_chunks_in_each_dimension);

                // If a loaded chunk is too *far* from player, move it into the unloaded chunks vector.
                const DirectX::XMINT3 chunk_index_3d = {
                    (i32)_chunk_index_3d.x - (i32)(number_of_chunks_in_each_dimension / 2u),
                    (i32)_chunk_index_3d.y - (i32)(number_of_chunks_in_each_dimension / 2u),
                    (i32)_chunk_index_3d.z - (i32)(number_of_chunks_in_each_dimension / 2u),
                };

                if ((u64)fabs(chunk_index_3d.x - current_chunk_3d_index.x) > chunk_render_distance ||
                    (u64)fabs(chunk_index_3d.y - current_chunk_3d_index.y) > chunk_render_distance ||
                    (u64)fabs(chunk_index_3d.z - current_chunk_3d_index.z) > chunk_render_distance)
                {
                    unloaded_chunks.emplace_back(loaded_chunks[i]);
                    loaded_chunks.erase(loaded_chunks.begin() + i);
                }
                else
                {
                    ++i;
                }
            }

            // Loop through the loaded chunks and render.
            for (auto &chunk : loaded_chunks)
            {
                const u64 chunk_index = chunk.m_chunk_index;

                const DirectX::XMUINT3 _chunk_index_3d =
                    convert_index_to_3d(chunk_index, number_of_chunks_in_each_dimension);

                const DirectX::XMINT3 chunk_index_3d = {
                    (i32)_chunk_index_3d.x - (i32)(number_of_chunks_in_each_dimension / 2u),
                    (i32)_chunk_index_3d.y - (i32)(number_of_chunks_in_each_dimension / 2u),
                    (i32)_chunk_index_3d.z - (i32)(number_of_chunks_in_each_dimension / 2u),
                };

                ChunkConstantBuffer chunk_constant_buffer_data = {
                    .transform_buffer = DirectX::XMMatrixTranslation(
                        chunk_index_3d.x * (i32)Chunk::number_of_voxels_per_dimension,
                        (chunk_index_3d.y - 1) * (i32)Chunk::number_of_voxels_per_dimension,
                        (chunk_index_3d.z * (i32)Chunk::number_of_voxels_per_dimension)),
                };

                const D3D12_VERTEX_BUFFER_VIEW chunk_vertex_buffer_view = {
                    .BufferLocation = chunk_vertex_buffers[chunk_index].m_buffer->GetGPUVirtualAddress(),
                    .SizeInBytes = (u32)(sizeof(VertexData) * chunk_vertices_counts[chunk_index]),
                    .StrideInBytes = sizeof(VertexData),
                };

                command_list->SetGraphicsRoot32BitConstants(1u, 16u, &chunk_constant_buffer_data.transform_buffer, 0u);
                command_list->IASetVertexBuffers(0u, 1u, &chunk_vertex_buffer_view);
                command_list->DrawInstanced(chunk_vertices_counts[chunk_index], 1u, 0u, 0u);
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
    }

    for (const auto &[chunk_index, chunk_cube] : chunk_cubes)
    {
        delete[] chunk_cube;
    }

    printf("Frames renderer :: %u", (u32)frame_count);

    renderer.flush_gpu();

    return 0;
}
