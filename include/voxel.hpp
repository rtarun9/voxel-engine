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
    CubeTypes cube_type{};
    bool active{true};
};

// A chunk (volume) of voxels.
struct Chunk
{
    Chunk()
    {
        cubes =
            new Cube[number_of_voxels_per_dimension * number_of_voxels_per_dimension * number_of_voxels_per_dimension];
    }

    virtual ~Chunk()
    {
        delete[] cubes;
    }

    static constexpr u32 number_of_voxels_per_dimension = 16u;

    Cube *cubes{};

    // Index into the renderer's vertex buffer vector.
    u32 vertex_buffer_index{};
};