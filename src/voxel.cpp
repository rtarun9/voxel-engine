#include "voxel-engine/voxel.hpp"

#include "shaders/interop/render_resources.hlsli"

#include <tracy/Tracy.hpp>

voxel_chunk_t::voxel_chunk_t()
{
    ZoneScoped;
    m_voxels = std::make_unique<voxel_t[]>(NUMBER_OF_VOXELS_PER_CHUNK);
}

voxel_chunk_t::voxel_chunk_t(voxel_chunk_t &&other) noexcept
    : m_voxels(std::move(other.m_voxels)), m_chunk_position(other.m_chunk_position),
      m_index_buffer_data(std::move(other.m_index_buffer_data)),
      m_color_buffer_data(std::move(other.m_color_buffer_data)),
      m_color_buffer_start_index_location(other.m_color_buffer_start_index_location),
      m_index_buffer_start_index_location(other.m_index_buffer_start_index_location)

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

        m_color_buffer_start_index_location = other.m_color_buffer_start_index_location;
        m_index_buffer_start_index_location = other.m_index_buffer_start_index_location;

        other.m_voxels = nullptr;
    }
    return *this;
}

voxel_chunk_manager_t::voxel_chunk_manager_t(rhi::renderer_t &renderer)
{
    ZoneScoped;
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

    for (size_t i = 0; i < MAX_NUMBER_OF_LOADED_CHUNKS; i++)
    {
        m_chunk_manager_buffer_offset_queue.push(chunk_manager_buffer_offset_t{
            .m_color_buffer_start_index_location = i,
            .m_index_buffer_start_index_location = 36u * NUMBER_OF_VOXELS_PER_CHUNK * i,
        });
    }
}

void voxel_chunk_manager_t::add_chunk_to_setup_stack(const voxel_chunk_position_t index)
{
    ZoneScoped;
    if (m_loaded_chunks.contains(index) || m_chunk_indices_that_are_being_setup.contains(index))
    {
        return;
    }

    m_chunk_indices_that_are_being_setup.insert(index);
    m_chunks_to_setup_stack.push(index);
}

void voxel_chunk_manager_t::create_chunks_from_setup_stack(rhi::renderer_t &renderer)
{
    ZoneScopedC(tracy::Color::AliceBlue);

    u64 chunks_that_are_setup = 0u;
    while (chunks_that_are_setup++ < voxel_chunk_manager_t::NUMBER_OF_CHUNKS_TO_CREATE_PER_FRAME &&
           !m_chunks_to_setup_stack.empty())
    {
        // First check if chunk_index_data is cached and ready for use.
        const voxel_chunk_position_t top = m_chunks_to_setup_stack.top();
        m_chunks_to_setup_stack.pop();

        m_setup_chunk_futures_queue.emplace(m_thread_pool.add_to_task_queue([this, &renderer, chunk_position = top]() {
            voxel_chunk_t setup_chunk_data{};

            // See if a vector of u16's is cached and available for use. This will prevent unnecessary creation of
            // std::vectors each frame. Iterate over each voxel in chunk and setup the chunk index and color buffer.
            std::vector<u16> chunk_index_data{};
            if (!m_cached_chunk_creation_resources.empty())
            {
                chunk_index_data = std::move(m_cached_chunk_creation_resources.front().indices_data);
                m_cached_chunk_creation_resources.pop();
            }
            else
            {
                chunk_index_data.reserve((size_t)36u * NUMBER_OF_VOXELS_PER_CHUNK);
            }

            chunk_index_data.clear();

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
                                    chunk_index_data.push_back(vertex_index + shared_index_buffer_offset);
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
                                    chunk_index_data.push_back(vertex_index + shared_index_buffer_offset);
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
                                    chunk_index_data.push_back(vertex_index + shared_index_buffer_offset);
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
                                    chunk_index_data.push_back(vertex_index + shared_index_buffer_offset);
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
                                    chunk_index_data.push_back(vertex_index + shared_index_buffer_offset);
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
                                    chunk_index_data.push_back(vertex_index + shared_index_buffer_offset);
                                }
                            }
                        }
                    }
                }
            }

            if (!chunk_index_data.empty())
            {

                setup_chunk_data.m_color_buffer_data = chunk_color;
                setup_chunk_data.m_index_buffer_data = std::move(chunk_index_data);
            }

            setup_chunk_data.m_chunk_position = chunk_position;
            return setup_chunk_data;
        }));
    }
}

void voxel_chunk_manager_t::transfer_chunks_from_setup_to_loaded_state()
{
    ZoneScoped;
    using namespace std::chrono_literals;

    u64 chunks_loaded = 0u;
    while (!m_setup_chunk_futures_queue.empty() &&
           chunks_loaded < voxel_chunk_manager_t::NUMBER_OF_CHUNKS_TO_LOAD_PER_FRAME)
    {
        auto &setup_chunk_data = m_setup_chunk_futures_queue.front();

        switch (std::future_status status = setup_chunk_data.wait_for(0s); status)
        {
        case std::future_status::timeout: {

            return;
        }
        break;

        case std::future_status::ready: {
            voxel_chunk_t chunk_to_load = std::move(setup_chunk_data.get());
            m_setup_chunk_futures_queue.pop();

            const auto chunk_index = chunk_to_load.m_chunk_position;

            m_chunk_indices_that_are_being_setup.erase(chunk_index);

            m_loaded_chunks[chunk_index] = std::move(chunk_to_load);

            const chunk_manager_buffer_offset_t buffer_offsets = m_chunk_manager_buffer_offset_queue.front();
            m_chunk_manager_buffer_offset_queue.pop();

            m_loaded_chunks[chunk_index].m_color_buffer_start_index_location =
                buffer_offsets.m_color_buffer_start_index_location;

            m_loaded_chunks[chunk_index].m_index_buffer_start_index_location =
                buffer_offsets.m_index_buffer_start_index_location;
        }
        break;
        }

        ++chunks_loaded;
    }
}
