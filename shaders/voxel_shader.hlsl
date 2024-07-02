#include "interop/render_resources.hlsli"

struct VSOutput
{
    float4 position : SV_Position;
    float4 color : VertexColor;
};

ConstantBuffer<VoxelRenderResources> render_resources : register(b0);

VSOutput vs_main(uint vertex_id : SV_VertexID)
{

    ConstantBuffer<SceneConstantBuffer> scene_buffer =
        ResourceDescriptorHeap[render_resources.scene_constant_buffer_index];
    ConstantBuffer<ChunkConstantBuffer> chunk_constant_buffer =
        ResourceDescriptorHeap[render_resources.chunk_constant_buffer_index];

    StructuredBuffer<float3> position_buffer = ResourceDescriptorHeap[chunk_constant_buffer.position_buffer_index];
    StructuredBuffer<float3> color_buffer = ResourceDescriptorHeap[chunk_constant_buffer.color_buffer_index];

    VSOutput output;
    output.position = mul((float4(position_buffer[vertex_id], 1.0f) + chunk_constant_buffer.translation_vector),
                          scene_buffer.view_projection_matrix);
    output.color = float4(color_buffer[0], 1.0f);

    return output;
}

float4 ps_main(VSOutput input) : SV_Target
{
    return input.color;
}
