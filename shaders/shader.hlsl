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

cbuffer ChunkTransformConstantBuffer : register(b1)
{
    row_major float4x4 transform_matrix;
    int is_chunk_culled;
};

VSOutput vs_main(VSInput input)
{
    VSOutput output;
    output.position = mul(mul(float4(input.position, 1.0f), transform_matrix), view_projection_matrix);

    if (is_chunk_culled == 0)
    {
        output.color = float4(1.0f, 0.0f, 0.0f, 1.0f);
    }
    else
    {
        output.color = float4(0.0f, 1.0f, 0.0f, 1.0f);
    }
    
    return output;
}

float4 ps_main(VSOutput input) : SV_Target
{
    return input.color;
}