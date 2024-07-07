#include "interop/render_resources.hlsli"

struct VSOutput
{
    float4 position : SV_Position;
    float4 color : VertexColor;
};

ConstantBuffer<TriangleRenderResources> render_resources : register(b0);

VSOutput vs_main(uint vertex_id : SV_VertexID)
{
    StructuredBuffer<float3> position_buffer = ResourceDescriptorHeap[render_resources.position_buffer_index];
    StructuredBuffer<float3> color_buffer = ResourceDescriptorHeap[render_resources.color_buffer_index];

    VSOutput output;
    output.position = float4(position_buffer[vertex_id], 1.0f);
    output.color = float4(color_buffer[vertex_id], 1.0f);

    return output;
}

float4 ps_main(VSOutput input) : SV_Target
{
    return input.color;
}