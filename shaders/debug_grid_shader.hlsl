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
    uint number_of_chunks_per_dimension;
    float grid_cube_dimension;
};

VSOutput vs_main(VSInput input, uint instanceID : SV_InstanceID)
{
    VSOutput output;

    // Compute the 3D index based on instance id.
    const uint z = instanceID / (number_of_chunks_per_dimension * number_of_chunks_per_dimension);
    const uint index_2d = instanceID - z * number_of_chunks_per_dimension * number_of_chunks_per_dimension;
    const uint y = index_2d / number_of_chunks_per_dimension;
    const uint x = index_2d % number_of_chunks_per_dimension;

    output.position = float4(input.position.xyz + (float3(x, y, z) - float3(number_of_chunks_per_dimension, number_of_chunks_per_dimension, number_of_chunks_per_dimension) / 2.0f) * grid_cube_dimension, 1.0f);
    output.position = mul(output.position, view_projection_matrix);

    output.color = float4(input.color, 1.0f);
    
    return output;
}

float4 ps_main(VSOutput input) : SV_Target
{
    return float4(0.05f, 0.05f, 0.05f, 1.0f);
}
