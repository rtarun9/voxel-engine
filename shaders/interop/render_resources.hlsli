#ifndef __RENDER_RESOURCES_HLSLI__
#define __RENDER_RESOURCES_HLSLI__

#ifdef __cplusplus

#define int3 DirectX::XMINT3
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
#define NUMBER_OF_VOXELS_PER_DIMENSION_IN_CHUNK  1

#define NUMBER_OF_VOXELS_PER_CHUNK (NUMBER_OF_VOXELS_PER_DIMENSION_IN_CHUNK * NUMBER_OF_VOXELS_PER_DIMENSION_IN_CHUNK * NUMBER_OF_VOXELS_PER_DIMENSION_IN_CHUNK)

// NOTE: These variables define how many chunks can be loaded at a given particular instant.
// If a new chunk is being added, it will replace an older chunk.
 // Why the *2 + 1? Because -x to x includes 0!!
#define CHUNK_RENDER_DISTANCE_PER_DIMENSION_EXTENT 12
#define CHUNK_RENDER_DISTANCE_PER_DIMENSION (CHUNK_RENDER_DISTANCE_PER_DIMENSION_EXTENT* 2 + 1)
#define MAX_NUMBER_OF_LOADED_CHUNKS (CHUNK_RENDER_DISTANCE_PER_DIMENSION  * CHUNK_RENDER_DISTANCE_PER_DIMENSION * CHUNK_RENDER_DISTANCE_PER_DIMENSION)
    
    // NOTE: Until it becomes a necessity, I will be storing non-indices in render resources for testing purposes. This is simply because to prevent creation of 'micro' constant buffers.
    struct triangle_render_resources_t
    {
        uint position_buffer_index;
        uint color_buffer_index;
    };

    struct voxel_render_resources_t
    {
        uint scene_constant_buffer_index;
        uint shared_chunk_position_buffer_index;
        uint color_buffer_index;
        uint color_start_location;
        uint padding;
        int3 chunk_position;
    };

ConstantBufferStruct
scene_constant_buffer_t
    {
        float4x4 view_matrix;
        float4x4 projection_matrix;

        // note(rtarun9) : Putting this here because scene depends on chunk edge length, which determines the AABB vertices.
        float4 aabb_vertices[8];
        float4 camera_position;
        float voxel_chunk_length;
    };

// D3D12_DRAW_INDEXED_ARGUMENTS has 5 32 bit members, which is why draw arguments is split into a uint4 and uint.
    struct gpu_indirect_command_t
    {
        voxel_render_resources_t voxel_render_resources;
        uint4 draw_arguments_1;
        uint draw_arguments_2;
    };

    struct gpu_cull_render_resources_t
    {
        uint number_of_chunks;
        uint indirect_command_srv_index;
        uint output_command_uav_index;
        uint scene_constant_buffer_index;
    };

}
#endif
