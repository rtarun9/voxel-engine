struct VSOutput
{
    float4 position : SV_Position;
    float4 color : VertexColor;
};

VSOutput vs_main(uint vertexId : SV_VertexID) : SV_POSITION
{
    static const float3 vertex_positions[3] =
    {
        float3(-0.5f, -0.5f, 0.0f),
        float3(0.0f, 0.5f, 0.0f),
        float3(0.5f, -0.5f, 0.0f)
    };

    static const float3 vertex_colors[3] =
    {
        float3(1.0f, 0.0f, 0.0f),
        float3(0.0f, 1.0f, 0.0f),
        float3(0.0f, 0.0f, 1.0f)
    };

    VSOutput output;
    output.position = float4(vertex_positions[vertexId], 1.0f);
    output.color = float4(vertex_colors[vertexId], 1.0f);
    
    return output;
}

float4 ps_main(VSOutput input) : SV_Target
{
    return input.color;
}