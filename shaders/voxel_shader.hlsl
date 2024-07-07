#include "interop/render_resources.hlsli"

struct VSOutput
{
    float4 position : SV_Position;
};

ConstantBuffer<VoxelRenderResources> render_resources : register(b0);

VSOutput vs_main(uint vertex_id : SV_VertexID)
{
    ConstantBuffer<ChunkConstantBuffer> chunk_constant_buffer =
        ResourceDescriptorHeap[render_resources.chunk_constant_buffer_index];

    ConstantBuffer<SceneConstantBuffer> scene_buffer =
        ResourceDescriptorHeap[render_resources.scene_constant_buffer_index];

    StructuredBuffer<float3> position_buffer = ResourceDescriptorHeap[chunk_constant_buffer.position_buffer_index];

    const float3 position =
        position_buffer[vertex_id] + -scene_buffer.camera_position.xyz + chunk_constant_buffer.translation_vector.xyz;

    VSOutput output;
    output.position = mul(mul(float4(position, 1.0f), scene_buffer.view_matrix), scene_buffer.projection_matrix);

    return output;
}

float4 ps_main(VSOutput input, uint primitive_id : SV_PrimitiveID) : SV_Target
{
    ConstantBuffer<ChunkConstantBuffer> chunk_constant_buffer =
        ResourceDescriptorHeap[render_resources.chunk_constant_buffer_index];

    StructuredBuffer<float3> color_buffer = ResourceDescriptorHeap[chunk_constant_buffer.color_buffer_index];
    return float4(color_buffer[primitive_id], 1.0f);
}
