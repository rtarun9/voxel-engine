#include "interop/render_resources.hlsli"

struct VSOutput
{
    float4 position : SV_Position;
};

ConstantBuffer<interop::voxel_render_resources_t> render_resources : register(b0);

VSOutput vs_main(uint vertex_id : SV_VertexID)
{
    ConstantBuffer<interop::scene_constant_buffer_t> scene_buffer =
        ResourceDescriptorHeap[render_resources.scene_constant_buffer_index];

    StructuredBuffer<float3> position_buffer =
        ResourceDescriptorHeap[render_resources.shared_chunk_position_buffer_index];

    const float4x4 view_projection_matrix = mul(scene_buffer.view_matrix, scene_buffer.projection_matrix);

    const float3 position = position_buffer[vertex_id] + render_resources.chunk_offset;

    VSOutput output;
    output.position = mul(float4(position, 1.0f), view_projection_matrix);

    return output;
}

float4 ps_main(VSOutput input, uint primitive_id : SV_PrimitiveID) : SV_Target
{
    // return float4(color_buffer[primitive_id], 1.0f);
    return float4(1.0f, 1.0f, 1.0f, 1.0f);
}
