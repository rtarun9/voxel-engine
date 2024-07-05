#include "interop/render_resources.hlsli"

ConstantBuffer<GPUCullRenderResources> render_resources : register(b0);

[numthreads(32, 1, 1)] void cs_main(uint dispatch_thread_id
                                    : SV_DispatchThreadID) {
    if (dispatch_thread_id < render_resources.number_of_chunks)
    {
        StructuredBuffer<GPUIndirectCommand> indirect_command =
            ResourceDescriptorHeap[render_resources.indirect_command_srv_index];

        AppendStructuredBuffer<GPUIndirectCommand> output_commands =
            ResourceDescriptorHeap[render_resources.output_command_uav_index];

        ConstantBuffer<SceneConstantBuffer> scene_constant_buffer =
            ResourceDescriptorHeap[render_resources.scene_constant_buffer_index];

        ConstantBuffer<ChunkConstantBuffer> chunk_constant_buffer =
            ResourceDescriptorHeap[indirect_command[dispatch_thread_id]
                                       .voxel_render_resources.chunk_constant_buffer_index];

        // For each vertex, find the clip space coord and check if AABB vertex is culled.
        uint culled_vertices = 0;
        for (int i = 0; i < 8; i++)
        {
            float4 clip_space_coords =
                mul(scene_constant_buffer.aabb_vertices[i] + chunk_constant_buffer.translation_vector,
                    scene_constant_buffer.view_projection_matrix);

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

        if (culled_vertices < 7)
        {
            output_commands.Append(indirect_command[dispatch_thread_id]);
        }
    }
}
