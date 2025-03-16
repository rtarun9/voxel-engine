
#include "voxel-engine/voxel.hpp"

#include "shaders/interop/render_resources.hlsli"

#include <tracy/Tracy.hpp>

voxel_chunk_t::voxel_chunk_t(const voxel_chunk_position_t chunk_position, const size_t index_buffer_offset,
                             const size_t color_buffer_offset)
    : m_chunk_position(chunk_position), m_index_buffer_offset(index_buffer_offset),
      m_color_buffer_offset(color_buffer_offset)
{
    ZoneScoped;

    m_voxels = std::make_unique<voxel_t[]>(NUMBER_OF_VOXELS_PER_CHUNK);
    m_index_buffer_data.reserve((size_t)36u * NUMBER_OF_VOXELS_PER_CHUNK);
}

voxel_chunk_t::voxel_chunk_t(voxel_chunk_t &&other) noexcept
    : m_voxels(std::move(other.m_voxels)), m_chunk_position(other.m_chunk_position),
      m_index_buffer_data(std::move(other.m_index_buffer_data)),
      m_color_buffer_data(std::move(other.m_color_buffer_data)), m_color_buffer_offset(other.m_color_buffer_offset),
      m_index_buffer_offset(other.m_index_buffer_offset)

{
    ZoneScoped;
    other.m_voxels = nullptr;
}

voxel_chunk_t &voxel_chunk_t::operator=(voxel_chunk_t &&other) noexcept
{
    ZoneScoped;
    if (this != &other)
    {
        m_voxels = std::move(other.m_voxels);

        m_chunk_position = other.m_chunk_position;

        m_index_buffer_data = std::move(other.m_index_buffer_data);
        m_color_buffer_data = std::move(other.m_color_buffer_data);

        m_color_buffer_offset = other.m_color_buffer_offset;
        m_index_buffer_offset = other.m_index_buffer_offset;

        other.m_voxels = nullptr;
    }
    return *this;
}

voxel_chunk_manager_t::voxel_chunk_manager_t(rhi::renderer_t &renderer)
{
    ZoneScoped;

    m_perlin = FastNoise::New<FastNoise::Perlin>();

    // Create the shared position buffer.
    std::vector<DirectX::XMFLOAT3> chunk_position_data{};
    chunk_position_data.reserve(size_t(8 * NUMBER_OF_VOXELS_PER_CHUNK));

    static constexpr std::array<DirectX::XMFLOAT3, 8> chunk_voxel_vertices{
        /*A*/ DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f),
        /*B*/ DirectX::XMFLOAT3(0.0f, voxel_t::EDGE_LENGTH, 0.0f),
        /*C*/ DirectX::XMFLOAT3(voxel_t::EDGE_LENGTH, voxel_t::EDGE_LENGTH, 0.0f),
        /*D*/ DirectX::XMFLOAT3(voxel_t::EDGE_LENGTH, 0.0f, 0.0f),
        /*E*/ DirectX::XMFLOAT3(0.0f, 0.0f, voxel_t::EDGE_LENGTH),
        /*F*/ DirectX::XMFLOAT3(0.0f, voxel_t::EDGE_LENGTH, voxel_t::EDGE_LENGTH),
        /*G*/ DirectX::XMFLOAT3(voxel_t::EDGE_LENGTH, voxel_t::EDGE_LENGTH, voxel_t::EDGE_LENGTH),
        /*H*/ DirectX::XMFLOAT3(voxel_t::EDGE_LENGTH, 0.0f, voxel_t::EDGE_LENGTH),
    };

    for (size_t i = 0; i < NUMBER_OF_VOXELS_PER_CHUNK; i++)
    {
        const DirectX::XMUINT3 index_3d = convert_to_3d(i, NUMBER_OF_VOXELS_PER_DIMENSION_IN_CHUNK);

        const DirectX::XMFLOAT3 offset = DirectX::XMFLOAT3(
            index_3d.x * voxel_t::EDGE_LENGTH, index_3d.y * voxel_t::EDGE_LENGTH, index_3d.z * voxel_t::EDGE_LENGTH);

        for (const auto &vertex : chunk_voxel_vertices)
        {
            chunk_position_data.push_back({vertex.x + offset.x, vertex.y + offset.y, vertex.z + offset.z});
        }
    }

    // NOTE: There are a lot of duplicates in this, because voxels share vertices, but in this shared position buffer,
    // each voxel has its own set of 8 vertices.
    const auto result = renderer.create_structured_buffer(chunk_position_data.data(), sizeof(DirectX::XMFLOAT3),
                                                          chunk_position_data.size(), L"shared chunk position buffer");

    renderer.m_copy_queue.flush_queue();
    m_shared_chunk_position_buffer = result.m_structured_buffer;

    // Create the index & color buffer.
    m_index_buffer = renderer.create_upload_structured_buffer(
        sizeof(u16), MAX_NUMBER_OF_LOADED_CHUNKS * NUMBER_OF_VOXELS_PER_CHUNK * 36u, L"Chunk manager index buffer");

    m_color_buffer = renderer.create_upload_structured_buffer(sizeof(DirectX::XMFLOAT3), MAX_NUMBER_OF_LOADED_CHUNKS,
                                                              L"Chunk manager index buffer");

    // Create vector of chunks.
    for (i32 z = -1 * CHUNK_RENDER_DISTANCE_PER_DIMENSION_EXTENT; z <= (i32)CHUNK_RENDER_DISTANCE_PER_DIMENSION_EXTENT;
         z++)
    {
        for (i32 y = -CHUNK_RENDER_DISTANCE_PER_DIMENSION_EXTENT; y <= (i32)CHUNK_RENDER_DISTANCE_PER_DIMENSION_EXTENT;
             y++)
        {
            for (i32 x = -CHUNK_RENDER_DISTANCE_PER_DIMENSION_EXTENT;
                 x <= (i32)CHUNK_RENDER_DISTANCE_PER_DIMENSION_EXTENT; x++)
            {
                m_chunk_render_distance_offsets.push_back(voxel_chunk_position_t{x, y, z});
            }
        }
    }
    std::sort(m_chunk_render_distance_offsets.begin(), m_chunk_render_distance_offsets.end(),
              [](const auto &a, const auto &b) {
                  return a.x * a.x + a.y * a.y + a.z * a.z < b.x * b.x + b.y * b.y + b.z * b.z;
              });

    m_chunk_render_distance_offsets.erase(
        std::unique(m_chunk_render_distance_offsets.begin(), m_chunk_render_distance_offsets.end()),
        m_chunk_render_distance_offsets.end());

    std::reverse(m_chunk_render_distance_offsets.begin(), m_chunk_render_distance_offsets.end());

    size_t index = 0;
    m_voxel_chunks.reserve(m_chunk_render_distance_offsets.size());

    for (const auto &chunk_render_distance_offset : m_chunk_render_distance_offsets)
    {
        m_voxel_chunks.push_back(
            voxel_chunk_t(chunk_render_distance_offset, (size_t)36u * NUMBER_OF_VOXELS_PER_CHUNK * index, index));
        m_unloaded_chunk_queue.push({m_voxel_chunks.back().m_chunk_position, index});

        ++index;
    }
}

void voxel_chunk_manager_t::add_chunk_to_setup_stack(const voxel_chunk_position_t index)
{
    ZoneScoped;
    if (m_loaded_chunk_to_index_map.contains(index) || m_chunks_being_setup_set.contains(index))
    {
        return;
    }

    if (m_chunks_to_setup_stack.size() > voxel_chunk_manager_t::MAX_SIZE_OF_CHUNKS_TO_SETUP_STACK)
    {
        const voxel_chunk_position_t element_being_removed = m_chunks_to_setup_stack.back();
        m_chunks_being_setup_set.erase(element_being_removed);

        m_chunks_to_setup_stack.pop_back();
    }

    m_chunks_to_setup_stack.push_front(index);
    m_chunks_being_setup_set.insert(index);
}

void voxel_chunk_manager_t::create_chunks_from_setup_stack(rhi::renderer_t &renderer)
{
    ZoneScopedC(tracy::Color::AliceBlue);

    u64 chunks_that_are_setup = 0u;
    while (chunks_that_are_setup++ < NUMBER_OF_CHUNKS_TO_CREATE_PER_FRAME && !m_chunks_to_setup_stack.empty())
    {
        ZoneScopedN("create_chunks_from_setup_stack iteration");

        // Check if there is a chunk that has to be unloaded. Only if this is the case, attempt to setup a new chunk.
        if (m_unloaded_chunk_queue.empty())
        {
            return;
        }

        const auto &[chunk_to_unload, index_of_chunk_being_unloaded] = m_unloaded_chunk_queue.front();
        m_unloaded_chunk_queue.pop();

        m_unloaded_chunks_set.erase(chunk_to_unload);

        // Chunks are marked as unloaded only when a new chunks is going to be loaded in its space.
        m_loaded_chunk_to_index_map.erase(chunk_to_unload);

        const voxel_chunk_position_t chunk_to_setup = m_chunks_to_setup_stack.front();
        m_chunks_to_setup_stack.pop_front();

        const auto meshing_algorithm =
            [this, &renderer](size_t index_of_chunk_being_unloaded, voxel_chunk_position_t chunk_to_unload,
                              voxel_chunk_position_t chunk_to_setup) -> voxel_chunk_setup_data_t {
            ZoneScopedN("Meshing algorithm MT");

            voxel_chunk_t &setup_chunk_data = m_voxel_chunks[index_of_chunk_being_unloaded];

            setup_chunk_data.m_index_buffer_data.clear();

            std::random_device random_device{};
            std::mt19937 engine(random_device());
            std::uniform_real_distribution<float> dist(0.0f, 1.0f);

            // note(rtarun9) : Only for demo purposes.
            const DirectX::XMFLOAT3 chunk_color = {
                dist(engine),
                dist(engine),
                dist(engine),
            };

            for (u32 z = 0; z < NUMBER_OF_VOXELS_PER_DIMENSION_IN_CHUNK; z++)
            {
                for (u32 y = 0; y < NUMBER_OF_VOXELS_PER_DIMENSION_IN_CHUNK; y++)
                {
                    for (u32 x = 0; x < NUMBER_OF_VOXELS_PER_DIMENSION_IN_CHUNK; x++)
                    {
                        const DirectX::XMUINT3 index_3d = {x, y, z};

                        const size_t i = convert_to_1d(index_3d, NUMBER_OF_VOXELS_PER_DIMENSION_IN_CHUNK);

                        const DirectX::XMINT3 voxel_index_3d_in_grid = DirectX::XMINT3{
                            (chunk_to_setup.x * (i32)NUMBER_OF_VOXELS_PER_DIMENSION_IN_CHUNK) + (i32)x,
                            (chunk_to_setup.y * (i32)NUMBER_OF_VOXELS_PER_DIMENSION_IN_CHUNK) + (i32)y,
                            (chunk_to_setup.z * (i32)NUMBER_OF_VOXELS_PER_DIMENSION_IN_CHUNK) + (i32)z,
                        };

                        const i32 height =
                            m_perlin->GenSingle2D(voxel_index_3d_in_grid.x, voxel_index_3d_in_grid.z, 0) *
                            MAX_TERRAIN_HEIGHT;

                        if (height >= voxel_index_3d_in_grid.y)
                        {
                            setup_chunk_data.m_voxels[i].m_active = true;
                        }
                    }
                }
            }

            for (u32 z = 0; z < NUMBER_OF_VOXELS_PER_DIMENSION_IN_CHUNK; z++)
            {
                for (u32 y = 0; y < NUMBER_OF_VOXELS_PER_DIMENSION_IN_CHUNK; y++)
                {
                    for (u32 x = 0; x < NUMBER_OF_VOXELS_PER_DIMENSION_IN_CHUNK; x++)
                    {
                        const DirectX::XMUINT3 index_3d = {x, y, z};

                        const size_t i = convert_to_1d(index_3d, NUMBER_OF_VOXELS_PER_DIMENSION_IN_CHUNK);

                        if (!setup_chunk_data.m_voxels[i].m_active)
                        {
                            continue;
                        }

                        const auto voxel_color = chunk_color;

                        const u16 shared_index_buffer_offset = i * 8u;

                        // Check if there is a voxel that blocks the front face of current voxel.
                        {

                            const bool is_front_face_covered =
                                (index_3d.z != 0 &&
                                 setup_chunk_data
                                     .m_voxels[convert_to_1d({index_3d.x, index_3d.y, index_3d.z - 1},
                                                             NUMBER_OF_VOXELS_PER_DIMENSION_IN_CHUNK)]
                                     .m_active);

                            if (!is_front_face_covered)
                            {
                                for (const auto &vertex_index : {0u, 1u, 2u, 0u, 2u, 3u})
                                {
                                    setup_chunk_data.m_index_buffer_data.push_back(vertex_index +
                                                                                   shared_index_buffer_offset);
                                }
                            }
                        }

                        // Check if there is a voxel that blocks the back face of current voxel.
                        {

                            const bool is_back_face_covered =
                                (index_3d.z != NUMBER_OF_VOXELS_PER_DIMENSION_IN_CHUNK - 1u &&
                                 setup_chunk_data
                                     .m_voxels[convert_to_1d({index_3d.x, index_3d.y, index_3d.z + 1},
                                                             NUMBER_OF_VOXELS_PER_DIMENSION_IN_CHUNK)]
                                     .m_active);

                            if (!is_back_face_covered)
                            {
                                for (const auto &vertex_index : {4u, 6u, 5u, 4u, 7u, 6u})
                                {
                                    setup_chunk_data.m_index_buffer_data.push_back(vertex_index +
                                                                                   shared_index_buffer_offset);
                                }
                            }
                        }

                        // Check if there is a voxel that blocks the left hand side face of current voxel.
                        {

                            const bool is_left_face_covered =
                                (index_3d.x != 0u &&
                                 setup_chunk_data
                                     .m_voxels[convert_to_1d({index_3d.x - 1, index_3d.y, index_3d.z},
                                                             NUMBER_OF_VOXELS_PER_DIMENSION_IN_CHUNK)]
                                     .m_active);

                            if (!is_left_face_covered)
                            {
                                for (const auto &vertex_index : {4u, 5u, 1u, 4u, 1u, 0u})
                                {
                                    setup_chunk_data.m_index_buffer_data.

                                        push_back(vertex_index + shared_index_buffer_offset);
                                }
                            }
                        }

                        // Check if there is a voxel that blocks the right hand side face of current
                        // voxel.
                        {

                            const bool is_right_face_covered =
                                (index_3d.x != NUMBER_OF_VOXELS_PER_DIMENSION_IN_CHUNK - 1u &&
                                 setup_chunk_data
                                     .m_voxels[convert_to_1d({index_3d.x + 1, index_3d.y, index_3d.z},
                                                             NUMBER_OF_VOXELS_PER_DIMENSION_IN_CHUNK)]
                                     .m_active);

                            if (!is_right_face_covered)
                            {
                                for (const auto &vertex_index : {3u, 2u, 6u, 3u, 6u, 7u})
                                {
                                    setup_chunk_data.m_index_buffer_data.

                                        push_back(vertex_index + shared_index_buffer_offset);
                                }
                            }
                        }

                        // Check if there is a voxel that blocks the top side face of current voxel.
                        {

                            const bool is_top_face_covered =
                                (index_3d.y != NUMBER_OF_VOXELS_PER_DIMENSION_IN_CHUNK - 1 &&
                                 setup_chunk_data
                                     .m_voxels[convert_to_1d({index_3d.x, index_3d.y + 1, index_3d.z},
                                                             NUMBER_OF_VOXELS_PER_DIMENSION_IN_CHUNK)]
                                     .m_active);

                            if (!is_top_face_covered)
                            {
                                for (const auto &vertex_index : {1u, 5u, 6u, 1u, 6u, 2u})
                                {
                                    setup_chunk_data.m_index_buffer_data.

                                        push_back(vertex_index + shared_index_buffer_offset);
                                }
                            }
                        }

                        // Check if there is a voxel that blocks the bottom side face of current voxel.
                        {

                            const bool is_bottom_face_covered =
                                (index_3d.y != 0u &&
                                 setup_chunk_data
                                     .m_voxels[convert_to_1d({index_3d.x, index_3d.y - 1, index_3d.z},
                                                             NUMBER_OF_VOXELS_PER_DIMENSION_IN_CHUNK)]
                                     .m_active);

                            if (!is_bottom_face_covered)
                            {
                                for (const auto &vertex_index : {4u, 0u, 3u, 4u, 3u, 7u})
                                {
                                    setup_chunk_data.m_index_buffer_data.

                                        push_back(vertex_index + shared_index_buffer_offset);
                                }
                            }
                        }
                    }
                }
            }

            voxel_chunk_setup_data_t setup_data = {
                .should_chunk_be_loaded = true,
                .chunk_position = chunk_to_setup,
                .index = index_of_chunk_being_unloaded,
            };

            if (!setup_chunk_data.m_index_buffer_data.empty())
            {
                setup_chunk_data.m_color_buffer_data = chunk_color;

                setup_chunk_data.m_chunk_position = chunk_to_unload;

                renderer.update_upload_structured_buffer(m_index_buffer, setup_chunk_data.m_index_buffer_data.data(),
                                                         setup_chunk_data.m_index_buffer_data.size() * sizeof(u16),
                                                         setup_chunk_data.m_index_buffer_offset * sizeof(u16));

                renderer.update_upload_structured_buffer(
                    m_color_buffer, &setup_chunk_data.m_color_buffer_data, sizeof(DirectX::XMFLOAT3),
                    setup_chunk_data.m_color_buffer_offset * sizeof(DirectX::XMFLOAT3));

                return setup_data;
            }

            setup_data.should_chunk_be_loaded = false;
            return setup_data;
        };

        {
            ZoneScopedN("Adding future to queue");
            m_setup_chunk_futures_queue.push(
                std::pair{renderer.m_copy_queue.m_monotonic_fence_value + 1,
                          m_thread_pool.add_to_task_queue(meshing_algorithm, index_of_chunk_being_unloaded,
                                                          chunk_to_unload, chunk_to_setup)});
        }
    }
}

void voxel_chunk_manager_t::transfer_chunks_from_setup_to_loaded_state(const u64 current_copy_queue_fence_value)
{
    using namespace std::chrono_literals;

    u64 chunks_loaded = 0u;
    while (!m_setup_chunk_futures_queue.empty() &&
           chunks_loaded < voxel_chunk_manager_t::NUMBER_OF_CHUNKS_TO_CREATE_PER_FRAME)
    {
        auto &setup_chunk_result = m_setup_chunk_futures_queue.front();

        switch (std::future_status status = setup_chunk_result.second.wait_for(0s); status)
        {
        case std::future_status::timeout: {

            return;
        }
        break;

        case std::future_status::ready: {
            // If this condition is satisfied, the buffers are ready, so chunk is ready to be loaded :)
            if (setup_chunk_result.first <= current_copy_queue_fence_value)
            {
                voxel_chunk_setup_data_t setup_data = setup_chunk_result.second.get();
                m_setup_chunk_futures_queue.pop();

                m_chunks_being_setup_set.erase(setup_data.chunk_position);

                m_loaded_chunk_to_index_map[setup_data.chunk_position] = setup_data.index;
            }
            else
            {
                return;
            }
        }
        break;
        }

        ++chunks_loaded;
    }
}
