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

cbuffer SceneConstantBuffer : register(b0)
{
    row_major float4x4 view_projection_matrix;
};

cbuffer TransformConstantBuffer : register(b1)
{
    float4x4 transform_buffer;
};

VSOutput vs_main(VSInput input)
{
    VSOutput output;
    output.position = mul(mul(float4(input.position, 1.0f), transform_buffer), view_projection_matrix);

    output.color = float4(input.color, 1.0f);
    
    return output;
}

float4 ps_main(VSOutput input) : SV_Target
{
    return float4(1.0f, 1.0f, 1.0f, 1.0f);
}
