#pragma once

#include "renderer.hpp"

// A WIP set of abstractions required for voxel rendering.
enum class CubeTypes : u8
{
    Default,
    Grass,
};

// A voxel (3d cube) abstraction.
struct Cube
{
    static constexpr float voxel_cube_dimension = 16.0f;

    CubeTypes m_cube_type{};
    bool m_active{false};
};

// A chunk (volume) of voxels.
// Each chunk knows its chunk index. This is to make loading / unloading easy.
struct Chunk
{
    static constexpr u32 number_of_voxels_per_dimension = 16u;
    static constexpr u64 number_of_voxels =
        number_of_voxels_per_dimension * number_of_voxels_per_dimension * number_of_voxels_per_dimension;

    static constexpr u64 chunk_length = Cube::voxel_cube_dimension * Chunk::number_of_voxels_per_dimension;

    u64 m_chunk_index{};
};

// The chunk manager has a hashmap of several data items where the key is the chunk index.
// A chunk can be in several states:
// (i) unloaded_chunks : Chunks that are loaded in memory but are not rendered.
// (ii) loaded_chunks : Chunks that are laoded in memory and are rendered.
// (iii) setup_chunks_indices : Chunks (specified by index) that have to be setup and loaded.
struct VertexData
{
    DirectX::XMFLOAT3 position{};
    DirectX::XMFLOAT3 color{};
};

class ChunkManager
{
  public:
    // The number of chunks in each dimension the player can look at.
    // The chunk loading / unloaded logic heavily depends on this value.
    static constexpr u64 chunk_render_distance = 9u;

    // When a chunk is unloaded, it is not rendered, but still in memory.
    // However, when a chunk is chunk_remove_distance away from the current chunk, it is removed fro memory.
    static constexpr u64 chunk_remove_distance = chunk_render_distance * 2u;

    // EXPERIMENTAL
    // If there are too many chunks to setup, it means that lot of the chunks to setup are probably not even near the
    // player anymore. Due to this, if the setup chunk indices queue reaches a particular size, clear the entire queue
    // so that only those chunks near the player will be loaded.
    static constexpr u64 max_setup_chunks_size =
        chunk_render_distance * chunk_render_distance * chunk_remove_distance * chunk_render_distance;

    static constexpr u64 number_of_chunks_in_each_dimension = 32u;
    static constexpr u64 number_of_chunks =
        number_of_chunks_in_each_dimension * number_of_chunks_in_each_dimension * number_of_chunks_in_each_dimension;

    // Each frame, only a certain number of chunks are setup.
    static constexpr u32 chunks_to_create_per_frame = 16u;

  public:
    std::vector<Chunk> m_loaded_chunks{};
    std::vector<Chunk> m_unloaded_chunks{};
    std::queue<u64> m_setup_chunk_indices{};

    std::unordered_map<u64, Buffer> m_chunk_vertex_buffers{};
    std::unordered_map<u64, std::vector<VertexData>> m_chunk_vertex_datas{};
    std::unordered_map<u64, size_t> m_chunk_vertices_counts{};
    std::unordered_map<u64, std::vector<Cube>> m_chunk_cubes{};
};
