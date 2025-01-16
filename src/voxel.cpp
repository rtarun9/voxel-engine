#include "voxel-engine/voxel.hpp"

#include "shaders/interop/render_resources.hlsli"

voxel_chunk_t::voxel_chunk_t()
{
    m_voxels = std::make_unique<voxel_t[]>(NUMBER_OF_VOXELS_PER_CHUNK);
}

voxel_chunk_t::voxel_chunk_t(voxel_chunk_t &&other) noexcept
    : m_voxels(std::move(other.m_voxels)), m_chunk_position(other.m_chunk_position),
      m_index_buffer(other.m_index_buffer), m_color_buffer(other.m_color_buffer)

{
    other.m_voxels = nullptr;
}

voxel_chunk_t &voxel_chunk_t::operator=(voxel_chunk_t &&other) noexcept
{
    if (this != &other)
    {
        m_voxels = std::move(other.m_voxels);

        m_chunk_position = other.m_chunk_position;

        m_index_buffer = other.m_index_buffer;
        m_color_buffer = other.m_color_buffer;

        other.m_voxels = nullptr;
    }
    return *this;
}

voxel_chunk_manager_t::voxel_chunk_manager_t(rhi::renderer_t &renderer)
{
    // Create the shared position buffer.
    std::vector<DirectX::XMFLOAT3> chunk_position_data{};

    static constexpr std::array<DirectX::XMFLOAT3, 8> chunk_voxel_vertices{
        DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f),
        DirectX::XMFLOAT3(0.0f, voxel_t::EDGE_LENGTH, 0.0f),
        DirectX::XMFLOAT3(voxel_t::EDGE_LENGTH, voxel_t::EDGE_LENGTH, 0.0f),
        DirectX::XMFLOAT3(voxel_t::EDGE_LENGTH, 0.0f, 0.0f),
        DirectX::XMFLOAT3(0.0f, 0.0f, voxel_t::EDGE_LENGTH),
        DirectX::XMFLOAT3(0.0f, voxel_t::EDGE_LENGTH, voxel_t::EDGE_LENGTH),
        DirectX::XMFLOAT3(voxel_t::EDGE_LENGTH, voxel_t::EDGE_LENGTH, voxel_t::EDGE_LENGTH),
        DirectX::XMFLOAT3(voxel_t::EDGE_LENGTH, 0.0f, voxel_t::EDGE_LENGTH),
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

    const auto result = renderer.create_structured_buffer(chunk_position_data.data(), sizeof(DirectX::XMFLOAT3),
                                                          chunk_position_data.size(), L"shared chunk position buffer");

    renderer.m_copy_queue.flush_queue();
    m_shared_chunk_position_buffer = result.m_structured_buffer;
}

void voxel_chunk_manager_t::add_chunk_to_setup_stack(const voxel_chunk_position_t index)
{
    if (m_loaded_chunks.contains(index) || m_chunk_indices_that_are_being_setup.contains(index))
    {
        return;
    }

    m_chunk_indices_that_are_being_setup.insert(index);
    m_chunks_to_setup_stack.push(index);
}

void voxel_chunk_manager_t::create_chunks_from_setup_stack(rhi::renderer_t &renderer)
{
    u64 chunks_that_are_setup = 0u;
    while (chunks_that_are_setup++ < voxel_chunk_manager_t::NUMBER_OF_CHUNKS_TO_CREATE_PER_FRAME &&
           !m_chunks_to_setup_stack.empty())
    {
        const voxel_chunk_position_t top = m_chunks_to_setup_stack.top();
        m_chunks_to_setup_stack.pop();

        m_setup_chunk_futures_queue.emplace(std::pair{
            renderer.m_copy_queue.m_monotonic_fence_value + 1,
            m_thread_pool.add_to_task_queue([this, &renderer, chunk_position = top]() {
                voxel_chunk_setup_data_t setup_chunk_data{};

                // Iterate over each voxel in chunk and setup the chunk index and color buffer.
                std::vector<u16> chunk_index_data{};
                std::vector<DirectX::XMFLOAT3> color_data{};

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

                            if (!setup_chunk_data.m_chunk.m_voxels[i].m_active)
                            {
                                continue;
                            }

                            const auto voxel_color = chunk_color;

                            const u16 shared_index_buffer_offset = i * 8u;

                            // Check if there is a voxel that blocks the front face of current voxel.
                            {

                                const bool is_front_face_covered =
                                    (index_3d.z != 0 &&
                                     setup_chunk_data.m_chunk
                                         .m_voxels[convert_to_1d({index_3d.x, index_3d.y, index_3d.z - 1},
                                                                 NUMBER_OF_VOXELS_PER_DIMENSION_IN_CHUNK)]
                                         .m_active);

                                if (!is_front_face_covered)
                                {
                                    color_data.emplace_back(voxel_color);
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
                                     setup_chunk_data.m_chunk
                                         .m_voxels[convert_to_1d({index_3d.x, index_3d.y, index_3d.z + 1},
                                                                 NUMBER_OF_VOXELS_PER_DIMENSION_IN_CHUNK)]
                                         .m_active);

                                color_data.emplace_back(voxel_color);
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
                                     setup_chunk_data.m_chunk
                                         .m_voxels[convert_to_1d({index_3d.x - 1, index_3d.y, index_3d.z},
                                                                 NUMBER_OF_VOXELS_PER_DIMENSION_IN_CHUNK)]
                                         .m_active);

                                color_data.emplace_back(voxel_color);
                                if (!is_left_face_covered)
                                {
                                    for (const auto &vertex_index : {4u, 5u, 1u, 4u, 1u, 0u})
                                    {
                                        chunk_index_data.push_back(vertex_index + shared_index_buffer_offset);
                                    }
                                }
                            }

                            // Check if there is a voxel that blocks the right hand side face of current voxel.
                            {

                                const bool is_right_face_covered =
                                    (index_3d.x != NUMBER_OF_VOXELS_PER_DIMENSION_IN_CHUNK - 1u &&
                                     setup_chunk_data.m_chunk
                                         .m_voxels[convert_to_1d({index_3d.x + 1, index_3d.y, index_3d.z},
                                                                 NUMBER_OF_VOXELS_PER_DIMENSION_IN_CHUNK)]
                                         .m_active);

                                if (!is_right_face_covered)
                                {
                                    color_data.emplace_back(voxel_color);
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
                                     setup_chunk_data.m_chunk
                                         .m_voxels[convert_to_1d({index_3d.x, index_3d.y + 1, index_3d.z},
                                                                 NUMBER_OF_VOXELS_PER_DIMENSION_IN_CHUNK)]
                                         .m_active);

                                if (!is_top_face_covered)
                                {
                                    color_data.emplace_back(voxel_color);
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
                                     setup_chunk_data.m_chunk
                                         .m_voxels[convert_to_1d({index_3d.x, index_3d.y - 1, index_3d.z},
                                                                 NUMBER_OF_VOXELS_PER_DIMENSION_IN_CHUNK)]
                                         .m_active);

                                if (!is_bottom_face_covered)
                                {
                                    color_data.emplace_back(voxel_color);
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

                    // TODO: Name each of these buffers.
                    setup_chunk_data.m_chunk_index_buffer =
                        renderer.create_index_buffer((void *)chunk_index_data.data(), sizeof(u16),
                                                     chunk_index_data.size(), std::wstring(L"Chunk Index buffer : "));
                    setup_chunk_data.m_chunk_color_buffer =
                        renderer.create_structured_buffer((void *)color_data.data(), sizeof(DirectX::XMFLOAT3),
                                                          color_data.size(), std::wstring(L"Chunk color buffer : "));
                }

                setup_chunk_data.m_chunk.m_chunk_position = chunk_position;
                return setup_chunk_data;
            })});
    }
}

void voxel_chunk_manager_t::transfer_chunks_from_setup_to_loaded_state(const u64 current_copy_queue_fence_value)
{
    using namespace std::chrono_literals;

    u64 chunks_loaded = 0u;
    while (!m_setup_chunk_futures_queue.empty() &&
           chunks_loaded < voxel_chunk_manager_t::NUMBER_OF_CHUNKS_TO_LOAD_PER_FRAME)
    {
        auto &setup_chunk_data = m_setup_chunk_futures_queue.front();

        switch (std::future_status status = setup_chunk_data.second.wait_for(0s); status)
        {
        case std::future_status::timeout: {

            return;
        }
        break;

        case std::future_status::ready: {
            // If this condition is satisfied, the buffers are ready, so chunk is ready to be loaded :)
            if (setup_chunk_data.first <= current_copy_queue_fence_value)
            {
                voxel_chunk_setup_data_t chunk_to_load = setup_chunk_data.second.get();
                m_setup_chunk_futures_queue.pop();

                const auto chunk_index = chunk_to_load.m_chunk.m_chunk_position;

                m_chunk_indices_that_are_being_setup.erase(chunk_index);
                m_loaded_chunks[chunk_index].m_chunk_position = chunk_to_load.m_chunk.m_chunk_position;
                m_loaded_chunks[chunk_index].m_index_buffer = chunk_to_load.m_chunk_index_buffer.m_index_buffer;
                m_loaded_chunks[chunk_index].m_color_buffer = chunk_to_load.m_chunk_color_buffer.m_structured_buffer;
                m_loaded_chunks[chunk_index].m_voxels = std::move(chunk_to_load.m_chunk.m_voxels);
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
