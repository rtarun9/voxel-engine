#ifndef __RENDER_RESOURCES_HLSLI__
#define __RENDER_RESOURCES_HLSLI__

#ifdef __cplusplus

#define float4x4 DirectX::XMMATRIX
#define float4 DirectX::XMFLOAT4
#define float3 DirectX::XMFLOAT3
#define uint u32
#define ConstantBufferStruct struct alignas(256)

#else

#pragma pack_matrix(row_major)
#define ConstantBufferStruct  struct

#endif

// clang-format off

struct TriangleRenderResources
{
    uint position_buffer_index;
    uint color_buffer_index;
};

ConstantBufferStruct
SceneConstantBuffer
{
    row_major float4x4 view_projection_matrix;
};

ConstantBufferStruct
ChunkConstantBuffer
{
    row_major float4x4 model_matrix;
};

struct VoxelRenderResources
{
    uint position_buffer_index;
    uint color_buffer_index;
    uint chunk_constant_buffer_index;

    uint chunk_index;

    uint scene_constant_buffer_index;
};

#endif