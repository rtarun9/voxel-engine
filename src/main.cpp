#include "pch.hpp"

#include "renderer.hpp"
#include "shader_compiler.hpp"
#include "types.hpp"
#include "window.hpp"

#include "common.hpp"

#include "camera.hpp"

#include "voxel.hpp"

#include "FastNoiseLite/FastNoiseLite.hpp"

int main()
{
    const Window window{};
    Renderer renderer(window.m_handle, window.m_width, window.m_height);

    // A vector of intermediate resources (required since intermediate buffers need to be in memory until the copy
    // resource and other functions are executed).
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> intermediate_resources{};
    intermediate_resources.reserve(4096);

    // Rendering related constants.
    // The maximum 'height' in y dimension generated terrain can be.
    static constexpr u32 max_terrain_height = 128u;

    // Create the resources required for rendering.

    // Vertex buffer setup.

    // Resources for the *debug draw*, a wireframe rendering to visualize all the chunks.
    // the debug draw will use instanced rendering. The 3D offset is determined in the shader itself.

    // Index buffer setup.
    Buffer debug_grid_index_buffer{};
    Buffer debug_grid_vertex_buffer{};
    D3D12_INDEX_BUFFER_VIEW debug_grid_index_buffer_view{};
    D3D12_VERTEX_BUFFER_VIEW debug_grid_vertex_buffer_view{};

    constexpr std::array<u16, 24> index_buffer_data = {0, 1, 1, 2, 2, 3, 3, 0, 4, 5, 5, 6,
                                                       6, 7, 7, 4, 0, 4, 1, 5, 2, 6, 3, 7};

    // Vertex buffer setup.
    // In this engine, the *voxel* is a point sample and represents(sort of) the FRONT LOWER LEFT point in the actual
    // CUBE.
    constexpr std::array<VertexData, 8> vertex_buffer_data = {
        VertexData{
            .position = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f),
            .color = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f),
        }, // 0
        VertexData{
            .position = DirectX::XMFLOAT3(0.0f, Chunk::chunk_length, 0.0f),
            .color = DirectX::XMFLOAT3(0.0f, 0.5f, 0.0f),
        }, // 1
        VertexData{
            .position = DirectX::XMFLOAT3(Chunk::chunk_length, Chunk::chunk_length, 0.0f),
            .color = DirectX::XMFLOAT3(0.5f, 0.5f, 0.0f),
        }, // 2
        VertexData{
            .position = DirectX::XMFLOAT3(Chunk::chunk_length, 0.0f, 0.0f),
            .color = DirectX::XMFLOAT3(0.5f, 0.0f, 0.0f),
        }, // 3
        VertexData{
            .position = DirectX::XMFLOAT3(0.0f, 0.0f, Chunk::chunk_length),
            .color = DirectX::XMFLOAT3(0.0f, 0.0f, 0.5f),
        }, // 4
        VertexData{
            .position = DirectX::XMFLOAT3(0.0f, Chunk::chunk_length, Chunk::chunk_length),
            .color = DirectX::XMFLOAT3(0.0f, 0.5f, 0.5f),
        }, // 5
        VertexData{
            .position = DirectX::XMFLOAT3(Chunk::chunk_length, Chunk::chunk_length, Chunk::chunk_length),
            .color = DirectX::XMFLOAT3(0.5f, 0.5f, 0.5f),
        }, // 6
        VertexData{
            .position = DirectX::XMFLOAT3(Chunk::chunk_length, 0.0f, Chunk::chunk_length),
            .color = DirectX::XMFLOAT3(0.5f, 0.0f, 0.5f),
        } // 7
    };

    {
        Renderer::BufferPair debug_grid_index_buffer_pair = renderer.create_buffer(
            (void *)&index_buffer_data, index_buffer_data.size() * sizeof(u16), BufferTypes::Static);

        intermediate_resources.emplace_back(debug_grid_index_buffer_pair.m_intermediate_buffer);
        debug_grid_index_buffer = Buffer{
            .m_buffer = debug_grid_index_buffer_pair.m_buffer,
        };

        // Create the index buffer view.
        debug_grid_index_buffer_view = {
            .BufferLocation = debug_grid_index_buffer.m_buffer->GetGPUVirtualAddress(),
            .SizeInBytes = sizeof(u16) * index_buffer_data.size(),
            .Format = DXGI_FORMAT_R16_UINT,
        };

        Renderer::BufferPair debug_grid_vertex_buffer_pair = renderer.create_buffer(
            (void *)&vertex_buffer_data, sizeof(VertexData) * vertex_buffer_data.size(), BufferTypes::Static);

        intermediate_resources.emplace_back(debug_grid_vertex_buffer_pair.m_intermediate_buffer);
        debug_grid_vertex_buffer = Buffer{
            .m_buffer = debug_grid_vertex_buffer_pair.m_buffer,
        };

        // Create the vertex buffer view.
        debug_grid_vertex_buffer_view = {
            .BufferLocation = debug_grid_vertex_buffer.m_buffer->GetGPUVirtualAddress(),
            .SizeInBytes = sizeof(VertexData) * vertex_buffer_data.size(),
            .StrideInBytes = sizeof(VertexData),
        };
    }

    // Setup for FastNoiseLite library.
    FastNoiseLite noise{};
    noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);

    ChunkManager chunk_manager{};

    const auto create_chunk = [&](const auto chunk_index) -> Chunk {
        // If chunk already exist, do not create again.
        if (chunk_manager.m_chunk_cubes.contains(chunk_index))
        {
            return Chunk{.m_chunk_index = -1u};
        }

        Chunk chunk{};
        chunk.m_chunk_index = chunk_index;

        chunk_manager.m_chunk_vertex_datas[chunk_index] = std::vector<VertexData>{};
        chunk_manager.m_chunk_cubes[chunk_index] = std::vector<Cube>(Chunk::number_of_voxels);

        const DirectX::XMFLOAT3 voxel_color =
            DirectX::XMFLOAT3(rand() / double(RAND_MAX), rand() / double(RAND_MAX), rand() / double(RAND_MAX));

        // For each voxel cube, first determine the y height. Everything with a y height that is > this value is set to
        // inactive.
        // To determine the index (i.e parameter to the GetNoise function), use the chunk index 3D + voxel index 3D.
        const DirectX::XMUINT3 _chunk_index_3d =
            convert_index_to_3d(chunk_index, ChunkManager::number_of_chunks_in_each_dimension);

        const DirectX::XMINT3 chunk_index_3d = {
            (i32)_chunk_index_3d.x - (i32)(ChunkManager::number_of_chunks_in_each_dimension / 2u),
            (i32)_chunk_index_3d.y - (i32)(ChunkManager::number_of_chunks_in_each_dimension / 2u),
            (i32)_chunk_index_3d.z - (i32)(ChunkManager::number_of_chunks_in_each_dimension / 2u),
        };

        for (u32 x = 0; x < Chunk::number_of_voxels_per_dimension; x++)
        {
            for (u32 z = 0; z < Chunk::number_of_voxels_per_dimension; z++)
            {
                const DirectX::XMINT3 voxel_index_3d_in_grid = DirectX::XMINT3{
                    (chunk_index_3d.x * (i32)Chunk::number_of_voxels_per_dimension) + (i32)x,
                    (chunk_index_3d.y * (i32)Chunk::number_of_voxels_per_dimension),
                    (chunk_index_3d.z * (i32)Chunk::number_of_voxels_per_dimension) + (i32)z,
                };

                const float height = (noise.GetNoise((float)voxel_index_3d_in_grid.x, (float)voxel_index_3d_in_grid.z) *
                                      (i32)max_terrain_height);

                // Check to see if voxel in the 'grid' / 'world' with y = height lines inside this chunk or not.
                if (height >= voxel_index_3d_in_grid.y)
                {
                    const u32 max_height_for_chunk =
                        (u32)min(height - voxel_index_3d_in_grid.y, (i32)Chunk::number_of_voxels_per_dimension);

                    for (u32 y = 0u; y < max_height_for_chunk; y++)
                    {

                        chunk_manager
                            .m_chunk_cubes[chunk_index][convert_index_to_1d(DirectX::XMINT3{(i32)x, (i32)(y), (i32)z},
                                                                            Chunk::number_of_voxels_per_dimension)]
                            .m_active = true;
                    }
                }
            }
        }

        for (u64 voxel_index = 0; voxel_index < Chunk::number_of_voxels; voxel_index++)
        {
            // Vertex buffer construction.
            const DirectX::XMUINT3 index = convert_index_to_3d(voxel_index, Chunk::number_of_voxels_per_dimension);

            if (chunk_manager.m_chunk_cubes[chunk_index][voxel_index].m_active)
            {
                const DirectX::XMFLOAT3 position_offset = DirectX::XMFLOAT3{
                    (float)index.x * Cube::voxel_cube_dimension,
                    (float)index.y * Cube::voxel_cube_dimension,
                    (float)index.z * Cube::voxel_cube_dimension,
                };

                // v1 : lower front left.
                const VertexData v1 = (VertexData{
                    .position = DirectX::XMFLOAT3(position_offset.x, position_offset.y, position_offset.z),
                    .color = voxel_color,
                });

                // v2 : upper front left.
                const VertexData v2 = (VertexData{
                    .position = DirectX::XMFLOAT3(position_offset.x, position_offset.y + Cube::voxel_cube_dimension,
                                                  position_offset.z),
                    .color = voxel_color,
                });

                // v3 : upper front right.
                const VertexData v3 = (VertexData{
                    .position = DirectX::XMFLOAT3(position_offset.x + Cube::voxel_cube_dimension,
                                                  position_offset.y + Cube::voxel_cube_dimension, position_offset.z),
                    .color = voxel_color,
                });

                // v4 : lower front right.
                const VertexData v4 = (VertexData{
                    .position = DirectX::XMFLOAT3(position_offset.x + Cube::voxel_cube_dimension, position_offset.y,
                                                  position_offset.z),
                    .color = voxel_color,
                });

                // v5 : lower back left.
                const VertexData v5 = (VertexData{
                    .position = DirectX::XMFLOAT3(position_offset.x, position_offset.y,
                                                  position_offset.z + Cube::voxel_cube_dimension),
                    .color = voxel_color,
                });

                // v6 : upper back left.
                const VertexData v6 = (VertexData{
                    .position = DirectX::XMFLOAT3(position_offset.x, position_offset.y + Cube::voxel_cube_dimension,
                                                  position_offset.z + Cube::voxel_cube_dimension),
                    .color = voxel_color,
                });

                // v7 : upper back right.
                const VertexData v7 = (VertexData{
                    .position = DirectX::XMFLOAT3(position_offset.x + Cube::voxel_cube_dimension,
                                                  position_offset.y + Cube::voxel_cube_dimension,
                                                  position_offset.z + Cube::voxel_cube_dimension),
                    .color = voxel_color,
                });

                // v8 : lower back right.
                const VertexData v8 = (VertexData{
                    .position = DirectX::XMFLOAT3(position_offset.x + Cube::voxel_cube_dimension, position_offset.y,
                                                  position_offset.z + Cube::voxel_cube_dimension),
                    .color = voxel_color,
                });

                // Logic for optimizing the data in vertex buffer.

                // If there is a cube that is a neighbour and sharing a "face" of the cube, those 2 triangles that
                // make up that "face" will not be seen and do not need to be added to vertex buffer.

                // Check if there is a cube in *FRONT* of current voxel cube.
                const u64 index_of_front_voxel =
                    convert_index_to_1d(DirectX::XMINT3{(i32)index.x, (i32)index.y, (i32)index.z - 1},
                                        Chunk::number_of_voxels_per_dimension);

                if (index.z == 0 ||
                    (index.z > 0u && !chunk_manager.m_chunk_cubes[chunk_index][index_of_front_voxel].m_active))
                {
                    chunk_manager.m_chunk_vertex_datas[chunk_index].emplace_back(v1);
                    chunk_manager.m_chunk_vertex_datas[chunk_index].emplace_back(v2);
                    chunk_manager.m_chunk_vertex_datas[chunk_index].emplace_back(v3);

                    chunk_manager.m_chunk_vertex_datas[chunk_index].emplace_back(v1);
                    chunk_manager.m_chunk_vertex_datas[chunk_index].emplace_back(v3);
                    chunk_manager.m_chunk_vertex_datas[chunk_index].emplace_back(v4);
                }

                // Check if there is a cube *behind* of current voxel cube.
                const u64 index_of_behind_voxel =
                    convert_index_to_1d(DirectX::XMINT3{(i32)index.x, (i32)index.y, (i32)index.z + 1},
                                        Chunk::number_of_voxels_per_dimension);

                if ((i32)index.z == (i32)(Chunk::number_of_voxels_per_dimension - 1u) ||
                    (index.z < Chunk::number_of_voxels_per_dimension &&
                     !chunk_manager.m_chunk_cubes[chunk_index][index_of_behind_voxel].m_active))
                {
                    chunk_manager.m_chunk_vertex_datas[chunk_index].emplace_back(v5);
                    chunk_manager.m_chunk_vertex_datas[chunk_index].emplace_back(v7);
                    chunk_manager.m_chunk_vertex_datas[chunk_index].emplace_back(v6);

                    chunk_manager.m_chunk_vertex_datas[chunk_index].emplace_back(v5);
                    chunk_manager.m_chunk_vertex_datas[chunk_index].emplace_back(v8);
                    chunk_manager.m_chunk_vertex_datas[chunk_index].emplace_back(v7);
                }

                // Check if there is a cube *left* of current voxel cube.
                const u64 index_of_left_voxel =
                    convert_index_to_1d(DirectX::XMINT3{(i32)index.x - 1, (i32)index.y, (i32)index.z},
                                        Chunk::number_of_voxels_per_dimension);

                if (index.x == 0 ||
                    (index.x > 0 && !chunk_manager.m_chunk_cubes[chunk_index][index_of_left_voxel].m_active))
                {
                    chunk_manager.m_chunk_vertex_datas[chunk_index].emplace_back(v5);
                    chunk_manager.m_chunk_vertex_datas[chunk_index].emplace_back(v6);
                    chunk_manager.m_chunk_vertex_datas[chunk_index].emplace_back(v2);

                    chunk_manager.m_chunk_vertex_datas[chunk_index].emplace_back(v5);
                    chunk_manager.m_chunk_vertex_datas[chunk_index].emplace_back(v2);
                    chunk_manager.m_chunk_vertex_datas[chunk_index].emplace_back(v1);
                }

                // Check if there is a cube *right* of current voxel cube.
                const u64 index_of_right_voxel =
                    convert_index_to_1d(DirectX::XMINT3{(i32)index.x + 1, (i32)index.y, (i32)index.z},
                                        Chunk::number_of_voxels_per_dimension);

                if ((i32)index.x == (i32)Chunk::number_of_voxels_per_dimension - 1 ||
                    (index.x < Chunk::number_of_voxels_per_dimension &&
                     !chunk_manager.m_chunk_cubes[chunk_index][index_of_right_voxel].m_active))
                {
                    chunk_manager.m_chunk_vertex_datas[chunk_index].emplace_back(v4);
                    chunk_manager.m_chunk_vertex_datas[chunk_index].emplace_back(v3);
                    chunk_manager.m_chunk_vertex_datas[chunk_index].emplace_back(v7);

                    chunk_manager.m_chunk_vertex_datas[chunk_index].emplace_back(v4);
                    chunk_manager.m_chunk_vertex_datas[chunk_index].emplace_back(v7);
                    chunk_manager.m_chunk_vertex_datas[chunk_index].emplace_back(v8);
                }

                // Check if there is a cube *top* of current voxel cube.
                const u64 index_of_top_voxel =
                    convert_index_to_1d(DirectX::XMINT3{(i32)index.x, (i32)index.y + 1, (i32)index.z},
                                        Chunk::number_of_voxels_per_dimension);

                if ((i32)index.y == (i32)Chunk::number_of_voxels_per_dimension - 1 ||
                    (index.y < Chunk::number_of_voxels_per_dimension &&
                     !chunk_manager.m_chunk_cubes[chunk_index][index_of_top_voxel].m_active))
                {
                    chunk_manager.m_chunk_vertex_datas[chunk_index].emplace_back(v2);
                    chunk_manager.m_chunk_vertex_datas[chunk_index].emplace_back(v6);
                    chunk_manager.m_chunk_vertex_datas[chunk_index].emplace_back(v7);

                    chunk_manager.m_chunk_vertex_datas[chunk_index].emplace_back(v2);
                    chunk_manager.m_chunk_vertex_datas[chunk_index].emplace_back(v7);
                    chunk_manager.m_chunk_vertex_datas[chunk_index].emplace_back(v3);
                }

                // Check if there is a cube *bottom* of current voxel cube.
                const u64 index_of_bottom_voxel =
                    convert_index_to_1d(DirectX::XMINT3{(i32)index.x, (i32)index.y - 1, (i32)index.z},
                                        Chunk::number_of_voxels_per_dimension);

                if (index.y == 0 ||
                    (index.y > 0 && !chunk_manager.m_chunk_cubes[chunk_index][index_of_bottom_voxel].m_active))
                {
                    chunk_manager.m_chunk_vertex_datas[chunk_index].emplace_back(v5);
                    chunk_manager.m_chunk_vertex_datas[chunk_index].emplace_back(v1);
                    chunk_manager.m_chunk_vertex_datas[chunk_index].emplace_back(v4);

                    chunk_manager.m_chunk_vertex_datas[chunk_index].emplace_back(v5);
                    chunk_manager.m_chunk_vertex_datas[chunk_index].emplace_back(v4);
                    chunk_manager.m_chunk_vertex_datas[chunk_index].emplace_back(v8);
                }
            }
        };

        if (!chunk_manager.m_chunk_vertex_datas[chunk_index].empty())

        {

            const Renderer::BufferPair vertex_buffer_pair = renderer.create_buffer(
                (void *)chunk_manager.m_chunk_vertex_datas[chunk_index].data(),
                sizeof(VertexData) * chunk_manager.m_chunk_vertex_datas[chunk_index].size(), BufferTypes::Static);

            intermediate_resources.emplace_back(vertex_buffer_pair.m_intermediate_buffer);
            chunk_manager.m_chunk_vertex_buffers[chunk_index] = Buffer{
                .m_buffer = vertex_buffer_pair.m_buffer,
            };

            chunk_manager.m_chunk_vertex_buffers[chunk_index].m_buffer->SetName(
                (std::wstring(L"Vertex buffer") + std::to_wstring(chunk_index)).c_str());

            const D3D12_RESOURCE_BARRIER copy_dest_to_vertex_buffer_barrier = {
                .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
                .Transition =
                    {
                        .pResource = chunk_manager.m_chunk_vertex_buffers[chunk_index].m_buffer.Get(),
                        .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                        .StateBefore = D3D12_RESOURCE_STATE_COPY_DEST,
                        .StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
                    },
            };

            renderer.m_command_list->ResourceBarrier(1u, &copy_dest_to_vertex_buffer_barrier);
        }
        else
        {
            chunk_manager.m_chunk_vertex_buffers[chunk_index] = Buffer{
                .m_buffer = nullptr,
            };
            chunk_manager.m_chunk_cubes[chunk_index].clear();
        }

        chunk_manager.m_chunk_vertices_counts[chunk_index] =
            (u64)chunk_manager.m_chunk_vertex_datas[chunk_index].size();

        return chunk;
    };

    // A function to remove chunk data entirely. This is done whenever a unloaded chunk is chunk_remove_distance away
    // from the current player chunk.
    const auto remove_chunk = [&](const u64 chunk_index) -> void {
        chunk_manager.m_chunk_cubes.erase(chunk_index);
        chunk_manager.m_chunk_vertex_datas.erase(chunk_index);
        chunk_manager.m_chunk_vertex_buffers.erase(chunk_index);
        chunk_manager.m_chunk_vertices_counts.erase(chunk_index);
    };

    // Create a 'scene' constant buffer.

    struct alignas(256) SceneConstantBuffer
    {
        DirectX::XMMATRIX view_projection_matrix{};
    };

    const Renderer::BufferPair scene_constant_buffer_pair =
        renderer.create_buffer(nullptr, sizeof(SceneConstantBuffer), BufferTypes::Dynamic);

    const Buffer scene_constant_buffer = Buffer{
        .m_buffer = scene_constant_buffer_pair.m_buffer,
    };

    u8 *scene_constant_buffer_ptr = scene_constant_buffer_pair.m_buffer_ptr;

    // Create the scene constant buffer descriptor.
    const D3D12_CPU_DESCRIPTOR_HANDLE scene_constant_buffer_descriptor_handle =
        renderer.create_constant_buffer_view(scene_constant_buffer, sizeof(SceneConstantBuffer));
    (void)scene_constant_buffer_descriptor_handle;

    // Create a 'chunk' constant buffer where per-chunk data is stored.
    struct alignas(256) ChunkConstantBuffer
    {
        DirectX::XMMATRIX transform_buffer{};
        u32 is_chunk_culled{0};
    };

    Buffer chunk_constant_buffer{};
    u8 *chunk_constant_buffer_ptr{};
    USE(chunk_constant_buffer_ptr);

    Renderer::BufferPair chunk_constant_buffers_pair =
        renderer.create_buffer(nullptr, sizeof(ChunkConstantBuffer), BufferTypes::Dynamic);

    chunk_constant_buffer = Buffer{
        .m_buffer = chunk_constant_buffers_pair.m_buffer,
    };

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
        // For chunk constant buffer (in future make this a constant buffer, but for testing purposes it is a inline
        // 32
        // bit root constants).
        D3D12_ROOT_PARAMETER1{
            .ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
            .Constants =
                D3D12_ROOT_CONSTANTS{
                    .ShaderRegister = 1u,
                    .RegisterSpace = 0u,
                    .Num32BitValues = 24u,
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
        D3D12_ROOT_PARAMETER1{
            .ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
            .Constants =
                D3D12_ROOT_CONSTANTS{
                    .ShaderRegister = 1u,
                    .RegisterSpace = 0u,
                    .Num32BitValues = 2u,
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
    const Microsoft::WRL::ComPtr<IDxcBlob> debug_grid_vertex_shader_blob =
        ShaderCompiler::compile(L"shaders/debug_grid_shader.hlsl", L"vs_main", L"vs_6_0");

    const Microsoft::WRL::ComPtr<IDxcBlob> debug_grid_pixel_shader_blob =
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

    // Precompute the offset which is used to check chunks around the camera (and determine if they are to be
    // created).
    std::vector<DirectX::XMINT3> offsets_to_check_chunk_around_player{};
    offsets_to_check_chunk_around_player.reserve(ChunkManager::chunk_render_distance *
                                                 ChunkManager::chunk_render_distance *
                                                 ChunkManager::chunk_render_distance);

    for (u32 dist = 0; dist < ChunkManager::chunk_render_distance; dist++)
    {
        for (i32 x = -(i32)dist; x <= (i32)dist; x++)
        {
            for (i32 z = -(i32)dist; z <= (i32)dist; z++)
            {
                // note(rtarun9) : Why doesnt just y = -dist and y = dist work?
                for (i32 y = -(i32)dist; y <= (i32)dist; y++)
                {
                    if ((x == -dist || x == dist) || (z == -dist || z == dist) || (y == -dist || y == dist))
                    {

                        const DirectX::XMINT3 chunk_3d_index = {
                            x,
                            y,
                            z,
                        };

                        offsets_to_check_chunk_around_player.emplace_back(chunk_3d_index);
                    }
                }
            }
        }
    }

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

        const float near_plane = 5.0f;
        const float far_plane = 10'000.0f;

        const DirectX::XMMATRIX view_matrix = camera.update_and_get_view_matrix(delta_time);
        const DirectX::XMMATRIX projection_matrix =
            DirectX::XMMatrixPerspectiveFovLH(vertical_fov, aspect_ratio, near_plane, far_plane);

        const DirectX::XMMATRIX view_projection_matrix = view_matrix * projection_matrix;

        // Update cbuffers.
        const SceneConstantBuffer buffer = {
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
        const D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle =
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
                    u32 number_of_chunks_per_dimension{};
                    float grid_cube_dimension{};
                };

                const DebugGridRootConstants root_constants = {

                    .number_of_chunks_per_dimension = ChunkManager::number_of_chunks_in_each_dimension,
                    .grid_cube_dimension = Chunk::chunk_length,
                };

                command_list->SetGraphicsRoot32BitConstants(1u, 2u, &root_constants, 0u);

                command_list->DrawIndexedInstanced(24u, ChunkManager::number_of_chunks, 0u, 0u, 0u);
            }
        }

        // Set the index buffer, pso and all config settings for chunk rendering.
        command_list->SetGraphicsRootSignature(chunks_root_signature.Get());
        command_list->SetPipelineState(chunks_pso.Get());

        command_list->SetGraphicsRootConstantBufferView(0u, scene_constant_buffer.m_buffer->GetGPUVirtualAddress());

        command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        DirectX::XMFLOAT3 camera_position{};
        DirectX::XMStoreFloat3(&camera_position, camera.m_camera_position);

        const DirectX::XMINT3 current_chunk_3d_index = {
            (i32)(floor((camera_position.x) / Chunk::chunk_length)),
            (i32)(floor((camera_position.y) / Chunk::chunk_length)),
            (i32)(floor((camera_position.z) / Chunk::chunk_length)),
        };

        const u64 current_chunk_index =
            convert_index_to_1d(current_chunk_3d_index, ChunkManager::number_of_chunks_in_each_dimension,
                                ChunkManager::number_of_chunks_in_each_dimension / 2u);
        USE(current_chunk_index);

        // Loop through the unloaded chunk vector.
        // and load the chunks that are close enough to the player.

        // note(rtarun9) : In future, convert this code to be more optimized (some sort of spatial data
        // structure).
        for (u32 i = 0; i < chunk_manager.m_unloaded_chunks.size();)
        {
            const DirectX::XMUINT3 _chunk_index_3d = convert_index_to_3d(
                chunk_manager.m_unloaded_chunks[i].m_chunk_index, ChunkManager::number_of_chunks_in_each_dimension);

            const DirectX::XMINT3 chunk_index_3d = {
                (i32)_chunk_index_3d.x - (i32)(ChunkManager::number_of_chunks_in_each_dimension / 2u),
                (i32)_chunk_index_3d.y - (i32)(ChunkManager::number_of_chunks_in_each_dimension / 2u),
                (i32)_chunk_index_3d.z - (i32)(ChunkManager::number_of_chunks_in_each_dimension / 2u),
            };

            if ((u64)fabs(chunk_index_3d.x - current_chunk_3d_index.x) <= ChunkManager::chunk_render_distance ||
                (u64)fabs(chunk_index_3d.y - current_chunk_3d_index.y) <= ChunkManager::chunk_render_distance ||
                (u64)fabs(chunk_index_3d.z - current_chunk_3d_index.z) <= ChunkManager::chunk_render_distance)
            {
                chunk_manager.m_loaded_chunks.emplace_back(chunk_manager.m_unloaded_chunks[i]);
                chunk_manager.m_unloaded_chunks.erase(chunk_manager.m_unloaded_chunks.begin() + i);
                ++i;
            }
            else
            {
                // If the unloaded chunk is chunk_remove_distance avway from current chunk, remove it.
                if ((u64)fabs(chunk_index_3d.x - current_chunk_3d_index.x) >= ChunkManager::chunk_remove_distance ||
                    (u64)fabs(chunk_index_3d.y - current_chunk_3d_index.y) >= ChunkManager::chunk_remove_distance ||
                    (u64)fabs(chunk_index_3d.z - current_chunk_3d_index.z) >= ChunkManager::chunk_remove_distance)
                {
                    chunk_manager.m_unloaded_chunks.erase(chunk_manager.m_unloaded_chunks.begin() + i);
                    remove_chunk(chunk_manager.m_unloaded_chunks[i].m_chunk_index);
                }

                ++i;
            }
        }

        for (i64 offset_index = offsets_to_check_chunk_around_player.size() - 1u; offset_index >= 0; --offset_index)
        {
            const DirectX::XMINT3 offset = offsets_to_check_chunk_around_player[(u64)offset_index];

            const u64 index = convert_index_to_1d(DirectX::XMINT3(offset.x + current_chunk_3d_index.x,
                                                                  offset.y + current_chunk_3d_index.y,
                                                                  offset.z + current_chunk_3d_index.z),
                                                  ChunkManager::number_of_chunks_in_each_dimension,
                                                  ChunkManager::number_of_chunks_in_each_dimension / 2u);

            // If caps lock key is pressed, do not load the current chunk.
            if (!(GetKeyState(VK_CAPITAL) & 0x0001) && !chunk_manager.m_chunk_cubes.contains(index))
            {
                chunk_manager.m_setup_chunk_indices.push(index);
            }
        }

        if (chunk_manager.m_setup_chunk_indices.size() >= ChunkManager::max_setup_chunks_size)
        {
            chunk_manager.m_setup_chunk_indices = {};
        }

        // Loop through loaded chunks, and whatever is too far, move to the unloaded vector.
        for (u64 i = 0; i < chunk_manager.m_loaded_chunks.size();)
        {
            const DirectX::XMUINT3 _chunk_index_3d = convert_index_to_3d(
                chunk_manager.m_loaded_chunks[i].m_chunk_index, ChunkManager::number_of_chunks_in_each_dimension);

            // If a loaded chunk is too *far* from player, move it into the unloaded chunks vector.
            const DirectX::XMINT3 chunk_index_3d = {
                (i32)_chunk_index_3d.x - (i32)(ChunkManager::number_of_chunks_in_each_dimension / 2u),
                (i32)_chunk_index_3d.y - (i32)(ChunkManager::number_of_chunks_in_each_dimension / 2u),
                (i32)_chunk_index_3d.z - (i32)(ChunkManager::number_of_chunks_in_each_dimension / 2u),
            };

            if ((u64)fabs(chunk_index_3d.x - current_chunk_3d_index.x) > ChunkManager::chunk_render_distance ||
                (u64)fabs(chunk_index_3d.y - current_chunk_3d_index.y) > ChunkManager::chunk_render_distance ||
                (u64)fabs(chunk_index_3d.z - current_chunk_3d_index.z) > ChunkManager::chunk_render_distance)
            {
                chunk_manager.m_unloaded_chunks.emplace_back(chunk_manager.m_loaded_chunks[i]);
                chunk_manager.m_loaded_chunks.erase(chunk_manager.m_loaded_chunks.begin() + i);
            }
            else
            {
                ++i;
            }
        }

        // For frustrum culling, this resource is used :
        // https://fgiesen.wordpress.com/2012/08/31/frustum-planes-from-the-projection-matrix/
        // clang-format off
        // Brief explanation : Say we have clip space coordinates (xc, yc, zc, wc). For the coord to be NOT clipped, it has to satisfy the equation,
        // -wc <= xc <= wc
        // -wc <= yc <= wc
        // 0 <= zc <= wc
        // (Where, (xc, yc, wc, zc) = (view_space_coord) * projection_matrix.
        // We get 6 equations from this, each of which map to a frustum plane.
        // Now, (xc, yc, zc, wc) = (view_space_coord) * projection_matrix
        // Moreoever, xc = v * (projection_matrix)column0
        // yc = v * (projection_matrix)column1, zc = v * (projection_matrix)column2, wc = v * (projection_matrix)column3
        // So, if you want to know if a point is NOT culled by left plane,
        // xc + wc >= 0
        // v * (projection_matrix)column0 + v * (projection_matrix)column3
        // v * (projection_matrix column + projection_matrix column3).
        // Using this equations, we can find if a point is to be culled or not.
        // clang-format on

        // Get the 6 combination of projection matrix vectors that is used to test against view space aabb coordinates.
        DirectX::XMFLOAT4X4 projection_matrix_4x4;
        DirectX::XMStoreFloat4x4(&projection_matrix_4x4, projection_matrix);

        const auto projection_column_0 =
            DirectX::XMVectorSet(projection_matrix_4x4.m[0][0], projection_matrix_4x4.m[1][0],
                                 projection_matrix_4x4.m[2][0], projection_matrix_4x4.m[3][0]);

        const auto projection_column_1 =
            DirectX::XMVectorSet(projection_matrix_4x4.m[0][1], projection_matrix_4x4.m[1][1],
                                 projection_matrix_4x4.m[2][1], projection_matrix_4x4.m[3][1]);

        const auto projection_column_2 =
            DirectX::XMVectorSet(projection_matrix_4x4.m[0][2], projection_matrix_4x4.m[1][2],
                                 projection_matrix_4x4.m[2][2], projection_matrix_4x4.m[3][2]);

        const auto projection_column_3 =
            DirectX::XMVectorSet(projection_matrix_4x4.m[0][3], projection_matrix_4x4.m[1][3],
                                 projection_matrix_4x4.m[2][3], projection_matrix_4x4.m[3][3]);

        const auto left_plane_projection_vector = projection_column_0 + projection_column_3;
        const auto right_plane_projection_vector = -projection_column_0 + projection_column_3;
        const auto near_plane_projection_vector = projection_column_2;
        const auto far_plane_projection_vector = projection_column_3 - projection_column_2;
        const auto top_plane_projection_vector = projection_column_3 - projection_column_1;
        const auto bottom_plane_projection_vector = projection_column_3 + projection_column_1;

        u64 chunks_rendered = 0u;
        u64 chunks_culled = 0u;
        for (auto &chunk : chunk_manager.m_loaded_chunks)
        {
            const u64 chunk_index = chunk.m_chunk_index;

            const DirectX::XMUINT3 _chunk_index_3d =
                convert_index_to_3d(chunk_index, ChunkManager::number_of_chunks_in_each_dimension);
            USE(_chunk_index_3d);

            const DirectX::XMINT3 chunk_index_3d = {
                (i32)_chunk_index_3d.x - (i32)(ChunkManager::number_of_chunks_in_each_dimension / 2u),
                (i32)_chunk_index_3d.y - (i32)(ChunkManager::number_of_chunks_in_each_dimension / 2u),
                (i32)_chunk_index_3d.z - (i32)(ChunkManager::number_of_chunks_in_each_dimension / 2u),
            };

            ChunkConstantBuffer chunk_constant_buffer_data = {
                .transform_buffer = DirectX::XMMatrixTranslation((chunk_index_3d.x) * (i32)Chunk::chunk_length,
                                                                 (chunk_index_3d.y) * (i32)Chunk::chunk_length,
                                                                 (chunk_index_3d.z) * (i32)Chunk::chunk_length),
                .is_chunk_culled = 0u,
            };

            const DirectX::XMVECTOR translation_vector = DirectX::XMVectorSet(
                chunk_index_3d.x * (i32)Chunk::chunk_length, chunk_index_3d.y * (i32)Chunk::chunk_length,
                chunk_index_3d.z * (i32)Chunk::chunk_length, 0.0f);

            if (chunk_manager.m_chunk_vertex_buffers[chunk_index].m_buffer)
            {
                const std::array<DirectX::XMVECTOR, 8> view_space_aabb_vertices = {
                    DirectX::XMVector4Transform(DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f) + translation_vector,
                                                view_matrix),

                    DirectX::XMVector4Transform(
                        DirectX::XMVectorSet(0.0f, Chunk::chunk_length, 0.0f, 1.0f) + translation_vector, view_matrix),

                    DirectX::XMVector4Transform(
                        DirectX::XMVectorSet(Chunk::chunk_length, Chunk::chunk_length, 0.0f, 1.0f) + translation_vector,
                        view_matrix),

                    DirectX::XMVector4Transform(
                        DirectX::XMVectorSet(Chunk::chunk_length, 0.0f, 0.0f, 1.0f) + translation_vector, view_matrix),

                    DirectX::XMVector4Transform(
                        DirectX::XMVectorSet(0.0f, 0.0f, Chunk::chunk_length, 1.0f) + translation_vector, view_matrix),

                    DirectX::XMVector4Transform(
                        DirectX::XMVectorSet(0.0f, Chunk::chunk_length, Chunk::chunk_length, 1.0f) + translation_vector,
                        view_matrix),

                    DirectX::XMVector4Transform(
                        DirectX::XMVectorSet(Chunk::chunk_length, Chunk::chunk_length, Chunk::chunk_length, 1.0f) +
                            translation_vector,
                        view_matrix),

                    DirectX::XMVector4Transform(
                        DirectX::XMVectorSet(Chunk::chunk_length, 0.0f, Chunk::chunk_length, 1.0f) + translation_vector,
                        view_matrix),
                };

                // Now, iterate over each of these vertices and perform the test. If atleast 1 equation satisfies, the
                // chunk is to NOT be culled.

                bool passed_frustum_culling_test = false;
                for (const auto &vertex : view_space_aabb_vertices)
                {
                    const bool left_plane_test =
                        DirectX::XMVectorGetX(DirectX::XMVector4Dot(vertex, left_plane_projection_vector)) > 0.0f;
                    const bool right_plane_test =
                        DirectX::XMVectorGetX(DirectX::XMVector4Dot(vertex, right_plane_projection_vector)) > 0.0f;
                    const float top_plane_test =
                        DirectX::XMVectorGetX(DirectX::XMVector4Dot(vertex, top_plane_projection_vector)) > 0.0f;
                    const bool bottom_plane_test =
                        DirectX::XMVectorGetX(DirectX::XMVector4Dot(vertex, bottom_plane_projection_vector)) > 0.0f;
                    const bool near_plane_test =
                        DirectX::XMVectorGetX(DirectX::XMVector4Dot(vertex, near_plane_projection_vector)) > 0.0f;
                    const bool far_plane_test =
                        DirectX::XMVectorGetX(DirectX::XMVector4Dot(vertex, far_plane_projection_vector)) > 0.0f;
                    // note(rtarun9) : This seems to be mathematically correct, but gives wrong results?
                    USE(far_plane_test);

                    if (left_plane_test || right_plane_test || top_plane_test || bottom_plane_test || near_plane_test)
                    {
                        passed_frustum_culling_test = true;
                        break;
                    }
                };

                if (passed_frustum_culling_test)
                {
                    chunk_constant_buffer_data.is_chunk_culled = 1u;
                    chunks_rendered++;
                }
                else
                {
                    chunk_constant_buffer_data.is_chunk_culled = 0u;
                    chunks_culled++;
                }

                const D3D12_VERTEX_BUFFER_VIEW chunk_vertex_buffer_view = {
                    .BufferLocation =
                        chunk_manager.m_chunk_vertex_buffers[chunk_index].m_buffer->GetGPUVirtualAddress(),
                    .SizeInBytes = (u32)(sizeof(VertexData) * chunk_manager.m_chunk_vertices_counts[chunk_index]),
                    .StrideInBytes = sizeof(VertexData),
                };
                command_list->SetGraphicsRoot32BitConstants(1u, 24u, &chunk_constant_buffer_data, 0u);
                command_list->IASetVertexBuffers(0u, 1u, &chunk_vertex_buffer_view);
                command_list->DrawInstanced(chunk_manager.m_chunk_vertices_counts[chunk_index], 1u, 0u, 0u);
            }
        }
        printf("Culled chunks : %zu Rendered chunks : %zu\n", chunks_culled, chunks_rendered);

        // Move a fixed number of chunks from the setup queue to loaded queue.
        for (size_t i = 0;
             (i < ChunkManager::chunks_to_create_per_frame) && !chunk_manager.m_setup_chunk_indices.empty();)
        {
            const u64 top = chunk_manager.m_setup_chunk_indices.top();
            chunk_manager.m_setup_chunk_indices.pop();

            if (const Chunk chunk = create_chunk(top); chunk.m_chunk_index != -1u)
            {
                chunk_manager.m_loaded_chunks.emplace_back(chunk);
                i++;
            }
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
