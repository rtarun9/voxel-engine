struct VSOutput
{
    float4 position : SV_Position;
    float4 color : VertexColor;
};

struct VSInput
{
    float3 position : POSITION;
    float3 color : COLOR;
};

VSOutput vs_main(VSInput input)
{
    VSOutput output;
    output.position = float4(input.position, 1.0f);
    output.color = float4(input.color, 1.0f);
    
    return output;
}

float4 ps_main(VSOutput input) : SV_Target
{
    return input.color;
}