#ifndef __RENDER_RESOURCES_HLSLI__
#define __RENDER_RESOURCES_HLSLI__

#ifdef __cplusplus

#define float4x4 DirectX::XMMATRIX
#define float4 DirectX::XMFLOAT4
#define float3 DirectX::XMFLOAT3
#define uint u32

#endif

struct TriangleRenderResources
{
    uint position_buffer_index;
    uint color_buffer_index;
};

#endif