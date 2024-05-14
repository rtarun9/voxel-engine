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

cbuffer TransformBufferCBuffer : register(b0)
{
    row_major float4x4 transform_buffer;
};

cbuffer InstanceTransformBufferCBuffer : register(b1)
{
    row_major float4x4 model_matrix[8u * 8u * 8u];
};

VSOutput vs_main(VSInput input, uint instance_id : SV_InstanceID)
{
    VSOutput output;
    output.position = mul(float4(input.position, 1.0f), mul(model_matrix[instance_id], transform_buffer));

    output.color = float4(input.color, 1.0f);
    
    return output;
}

float4 ps_main(VSOutput input) : SV_Target
{
    return input.color;
}