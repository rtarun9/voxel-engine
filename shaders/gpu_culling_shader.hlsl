#include "interop/render_resources.hlsli"

ConstantBuffer<interop::gpu_cull_render_resources_t> render_resources : register(b0);

[numthreads(32, 1, 1)] void cs_main(uint dispatch_thread_id
                                    : SV_DispatchThreadID) {
    if (dispatch_thread_id < render_resources.number_of_chunks)
    {
        StructuredBuffer<interop::gpu_indirect_command_t> indirect_command =
            ResourceDescriptorHeap[render_resources.indirect_command_srv_index];

        AppendStructuredBuffer<interop::gpu_indirect_command_t> output_commands =
            ResourceDescriptorHeap[render_resources.output_command_uav_index];

        ConstantBuffer<interop::scene_constant_buffer_t> scene_constant_buffer =
            ResourceDescriptorHeap[render_resources.scene_constant_buffer_index];

        // For each vertex, find the clip space coord and check if AABB vertex is culled.
        uint culled_vertices = 0;
        for (int i = 0; i < 8; i++)
        {
            float4 clip_space_coords =
                mul(scene_constant_buffer.aabb_vertices[i] +
                        float4(indirect_command[dispatch_thread_id].voxel_render_resources.chunk_position *
                                   scene_constant_buffer.voxel_chunk_length,
                               1.0f),
                    mul(scene_constant_buffer.view_matrix, scene_constant_buffer.projection_matrix));

            clip_space_coords.x /= clip_space_coords.w;
            clip_space_coords.y /= clip_space_coords.w;
            clip_space_coords.z /= clip_space_coords.w;

            bool is_visible =
                (-clip_space_coords.w <= clip_space_coords.x) && (clip_space_coords.x <= clip_space_coords.w) &&
                (-clip_space_coords.w <= clip_space_coords.y) && (clip_space_coords.y <= clip_space_coords.w) &&
                (0 <= clip_space_coords.z) && (clip_space_coords.z <= clip_space_coords.w);

            if (!is_visible)
            {
                ++culled_vertices;
            }
        }

        if (culled_vertices <= 7)
        {
            output_commands.Append(indirect_command[dispatch_thread_id]);
        }
    }
}
