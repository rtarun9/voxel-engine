#pragma once

// A WIP set of abstractions required for voxel rendering.
enum class CubeTypes : u8
{
    Default,
    Grass,
};

// A voxel (3d cube) abstraction.
struct Cube
{
    CubeTypes m_cube_type{};
    bool m_active{true};
};

// A chunk (volume) of voxels.
// Each chunk knows its chunk index. This is to make loading / unloading easy.
struct Chunk
{
    Chunk()
    {
        m_cubes =
            new Cube[number_of_voxels_per_dimension * number_of_voxels_per_dimension * number_of_voxels_per_dimension];
    }

    ~Chunk()
    {
        // NOTE : MEMORY LEAKING ON PURPOSE
        // delete[] m_cubes;
    }

    static constexpr u32 number_of_voxels_per_dimension = 16u;
    static constexpr u64 number_of_voxels =
        number_of_voxels_per_dimension * number_of_voxels_per_dimension * number_of_voxels_per_dimension;

    Cube *m_cubes{};
    u64 m_chunk_index{};
};
