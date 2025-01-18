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

    const float3 position = position_buffer[vertex_id] +
                            float3(0.0f, 0.0f, render_resources.chunk_position.z * scene_buffer.voxel_chunk_length);

    VSOutput output;
    output.position = mul(float4(position, 1.0f), view_projection_matrix);

    return output;
}

float4 ps_main(VSOutput input, uint primitive_id : SV_PrimitiveID) : SV_Target
{
    StructuredBuffer<float3> color_buffer = ResourceDescriptorHeap[render_resources.color_buffer_index];

    // The idea behind using primitive ID here is that when creating the color buffer for voxel, you can use a single
    // color value for 3 vertices. But for now, the entire chunk has the same color.
    return float4(color_buffer[primitive_id], 1.0f);
}
