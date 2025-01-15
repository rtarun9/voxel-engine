#ifndef __RENDER_RESOURCES_HLSLI__
#define __RENDER_RESOURCES_HLSLI__

#ifdef __cplusplus

#define float4x4 DirectX::XMMATRIX
#define float4 DirectX::XMFLOAT4
#define float3 DirectX::XMFLOAT3
#define uint u32
#define uint4 DirectX::XMUINT4
#define ConstantBufferStruct struct alignas(256)

#else

#pragma pack_matrix(row_major)
#define ConstantBufferStruct  struct

#endif

// clang-format off
namespace interop
{
#define MAX_CHUNKS_IN_WORLD 

#define NUMBER_OF_VOXELS_PER_DIMENSION_IN_CHUNK  8u
#define NUMBER_OF_VOXELS_PER_CHUNK (NUMBER_OF_VOXELS_PER_DIMENSION_IN_CHUNK * NUMBER_OF_VOXELS_PER_DIMENSION_IN_CHUNK * NUMBER_OF_VOXELS_PER_DIMENSION_IN_CHUNK)

#define NUMBER_OF_CHUNKS_PER_DIMENSION_IN_WORLD 12
#define NUMBER_OF_CHUNKS_IN_WORLD (NUMBER_OF_CHUNKS_PER_DIMENSION_IN_WORLD * NUMBER_OF_CHUNKS_PER_DIMENSION_IN_WORLD * NUMBER_OF_CHUNKS_PER_DIMENSION_IN_WORLD)

// NOTE: These variables define how many chunks can be loaded at a given particular instant.
// If a new chunk is being added, it will replace an older chunk.
#define NUMER_OF_LOADED_CHUNKS_PER_DIMENSION 6
#define NUMBER_OF_LOADED_CHUNKS_IN_WORLD (NUMBER_OF_LOADED_CHUNKS_PER_DIMENSION_IN_WORLD * NUMBER_OF_LOADED_CHUNKS_PER_DIMENSION_IN_WORLD * NUMBER_OF_LOADED_CHUNKS_PER_DIMENSION_IN_WORLD)
    
    struct TriangleRenderResources
    {
        uint position_buffer_index;
        uint color_buffer_index;
    };

    struct VoxelRenderResources
    {
        uint scene_constant_buffer_index;
        uint chunk_constant_buffer_index;
    };

ConstantBufferStruct
SceneConstantBuffer
    {
        float4x4 view_matrix;
        float4x4 projection_matrix;

    // note(rtarun9) : Putting this here because scene depends on chunk edge length, which determines the AABB vertices.
        float4 aabb_vertices[8];
        float4 camera_position;
    };

ConstantBufferStruct
chunk_manager_constant_buffer_t
    {
        uint4 translation_vector;

        uint position_buffer_index;

        uint color_buffer_index;
    };

// D3D12_DRAW_INDEXED_ARGUMENTS has 5 32 bit members, which is why draw arguments is split into a uint4 and uint.
    struct GPUIndirectCommand
    {
        VoxelRenderResources voxel_render_resources;
        uint4 index_buffer_view;
        uint4 draw_arguments_1;
        uint draw_arguments_2;
        uint padding;
    };

    struct GPUCullRenderResources
    {
        uint number_of_chunks;
        uint indirect_command_srv_index;
        uint output_command_uav_index;
        uint scene_constant_buffer_index;
    };

}
#endif