#include "voxel-engine/voxel.hpp"

Chunk::Chunk()
{
    m_voxels = new Voxel[NUMBER_OF_VOXELS];
}
Chunk::Chunk(Chunk &&other) noexcept : m_voxels(std::move(other.m_voxels)), m_chunk_index(other.m_chunk_index)

{
    other.m_voxels = nullptr;
}

Chunk &Chunk::operator=(Chunk &&other) noexcept
{
    this->m_voxels = std::move(other.m_voxels);
    this->m_chunk_index = other.m_chunk_index;

    other.m_voxels = nullptr;

    return *this;
}

Chunk::~Chunk()
{
    if (m_voxels)
    {
        delete[] m_voxels;
    }
}

ChunkManager::SetupChunkData ChunkManager::internal_mt_setup_chunk(Renderer &renderer, const size_t index)
{
    SetupChunkData setup_chunk_data{};

    // Iterate over each voxel in chunk and setup the chunk common position buffer.
    std::vector<DirectX::XMFLOAT3> position_data{};

    static constexpr std::array<DirectX::XMFLOAT3, 8> voxel_vertices_data{
        DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f),
        DirectX::XMFLOAT3(0.0f, Voxel::EDGE_LENGTH, 0.0f),
        DirectX::XMFLOAT3(Voxel::EDGE_LENGTH, Voxel::EDGE_LENGTH, 0.0f),
        DirectX::XMFLOAT3(Voxel::EDGE_LENGTH, 0.0f, 0.0f),
        DirectX::XMFLOAT3(0.0f, 0.0f, Voxel::EDGE_LENGTH),
        DirectX::XMFLOAT3(0.0f, Voxel::EDGE_LENGTH, Voxel::EDGE_LENGTH),
        DirectX::XMFLOAT3(Voxel::EDGE_LENGTH, Voxel::EDGE_LENGTH, Voxel::EDGE_LENGTH),
        DirectX::XMFLOAT3(Voxel::EDGE_LENGTH, 0.0f, Voxel::EDGE_LENGTH),
    };

    const DirectX::XMUINT3 chunk_index_3d = convert_to_3d(index, ChunkManager::NUMBER_OF_CHUNKS_PER_DIMENSION);

    const DirectX::XMFLOAT3 chunk_offset =
        DirectX::XMFLOAT3(chunk_index_3d.x * Voxel::EDGE_LENGTH * Chunk::NUMBER_OF_VOXELS_PER_DIMENSION,
                          chunk_index_3d.y * Voxel::EDGE_LENGTH * Chunk::NUMBER_OF_VOXELS_PER_DIMENSION,
                          chunk_index_3d.z * Voxel::EDGE_LENGTH * Chunk::NUMBER_OF_VOXELS_PER_DIMENSION);

    for (size_t i = 0; i < Chunk::NUMBER_OF_VOXELS; i++)
    {
        const DirectX::XMUINT3 index_3d = convert_to_3d(i, Chunk::NUMBER_OF_VOXELS_PER_DIMENSION);
        const DirectX::XMFLOAT3 offset = DirectX::XMFLOAT3(index_3d.x * Voxel::EDGE_LENGTH + chunk_offset.x,
                                                           index_3d.y * Voxel::EDGE_LENGTH + chunk_offset.y,
                                                           index_3d.z * Voxel::EDGE_LENGTH + chunk_offset.z);

        // Check if there is a voxel that blocks the front face of current voxel.
        {

            const bool is_front_face_covered =
                (index_3d.z != 0 && setup_chunk_data.m_chunk
                                        .m_voxels[convert_to_1d({index_3d.x, index_3d.y, index_3d.z - 1},
                                                                Chunk::NUMBER_OF_VOXELS_PER_DIMENSION)]
                                        .m_active);

            if (!is_front_face_covered)
            {
                for (const auto &vertex_index : {0u, 1u, 2u, 0u, 2u, 3u})
                {
                    const auto &vertex = voxel_vertices_data[vertex_index];
                    position_data.emplace_back(
                        DirectX::XMFLOAT3{vertex.x + offset.x, vertex.y + offset.y, vertex.z + offset.z});
                }
            }
        }

        // Check if there is a voxel that blocks the back face of current voxel.
        {

            const bool is_back_face_covered = (index_3d.z != Chunk::NUMBER_OF_VOXELS_PER_DIMENSION - 1u &&
                                               setup_chunk_data.m_chunk
                                                   .m_voxels[convert_to_1d({index_3d.x, index_3d.y, index_3d.z + 1},
                                                                           Chunk::NUMBER_OF_VOXELS_PER_DIMENSION)]
                                                   .m_active);

            if (!is_back_face_covered)
            {
                for (const auto &vertex_index : {4u, 6u, 5u, 4u, 7u, 6u})
                {
                    const auto &vertex = voxel_vertices_data[vertex_index];
                    position_data.emplace_back(
                        DirectX::XMFLOAT3{vertex.x + offset.x, vertex.y + offset.y, vertex.z + offset.z});
                }
            }
        }

        // Check if there is a voxel that blocks the left hand side face of current voxel.
        {

            const bool is_left_face_covered =
                (index_3d.x != 0u && setup_chunk_data.m_chunk
                                         .m_voxels[convert_to_1d({index_3d.x - 1, index_3d.y, index_3d.z},
                                                                 Chunk::NUMBER_OF_VOXELS_PER_DIMENSION)]
                                         .m_active);

            if (!is_left_face_covered)
            {
                for (const auto &vertex_index : {4u, 5u, 1u, 4u, 1u, 0u})
                {
                    const auto &vertex = voxel_vertices_data[vertex_index];
                    position_data.emplace_back(
                        DirectX::XMFLOAT3{vertex.x + offset.x, vertex.y + offset.y, vertex.z + offset.z});
                }
            }
        }

        // Check if there is a voxel that blocks the right hand side face of current voxel.
        {

            const bool is_right_face_covered = (index_3d.x != Chunk::NUMBER_OF_VOXELS_PER_DIMENSION - 1u &&
                                                setup_chunk_data.m_chunk
                                                    .m_voxels[convert_to_1d({index_3d.x + 1, index_3d.y, index_3d.z},
                                                                            Chunk::NUMBER_OF_VOXELS_PER_DIMENSION)]
                                                    .m_active);

            if (!is_right_face_covered)
            {
                for (const auto &vertex_index : {3u, 2u, 6u, 3u, 6u, 7u})
                {
                    const auto &vertex = voxel_vertices_data[vertex_index];
                    position_data.emplace_back(
                        DirectX::XMFLOAT3{vertex.x + offset.x, vertex.y + offset.y, vertex.z + offset.z});
                }
            }
        }

        // Check if there is a voxel that blocks the top side face of current voxel.
        {

            const bool is_top_face_covered = (index_3d.y != Chunk::NUMBER_OF_VOXELS_PER_DIMENSION - 1 &&
                                              setup_chunk_data.m_chunk
                                                  .m_voxels[convert_to_1d({index_3d.x, index_3d.y + 1, index_3d.z},
                                                                          Chunk::NUMBER_OF_VOXELS_PER_DIMENSION)]
                                                  .m_active);

            if (!is_top_face_covered)
            {
                for (const auto &vertex_index : {1u, 5u, 6u, 1u, 6u, 2u})
                {
                    const auto &vertex = voxel_vertices_data[vertex_index];
                    position_data.emplace_back(
                        DirectX::XMFLOAT3{vertex.x + offset.x, vertex.y + offset.y, vertex.z + offset.z});
                }
            }
        }

        // Check if there is a voxel that blocks the bottom side face of current voxel.
        {

            const bool is_bottom_face_covered =
                (index_3d.y != 0u && setup_chunk_data.m_chunk
                                         .m_voxels[convert_to_1d({index_3d.x, index_3d.y - 1, index_3d.z},
                                                                 Chunk::NUMBER_OF_VOXELS_PER_DIMENSION)]
                                         .m_active);

            if (!is_bottom_face_covered)
            {
                for (const auto &vertex_index : {4u, 0u, 3u, 4u, 3u, 7u})
                {
                    const auto &vertex = voxel_vertices_data[vertex_index];
                    position_data.emplace_back(
                        DirectX::XMFLOAT3{vertex.x + offset.x, vertex.y + offset.y, vertex.z + offset.z});
                }
            }
        }
    }

    setup_chunk_data.m_chunk_position_data = std::move(position_data);
    setup_chunk_data.m_chunk_color_data = {
        rand() / (float)RAND_MAX,
        rand() / (float)RAND_MAX,
        rand() / (float)RAND_MAX,
    };

    if (!setup_chunk_data.m_chunk_position_data.empty())
    {
        setup_chunk_data.m_chunk_position_buffer =
            renderer.create_structured_buffer((void *)setup_chunk_data.m_chunk_position_data.data(),
                                              sizeof(DirectX::XMFLOAT3), setup_chunk_data.m_chunk_position_data.size());
        setup_chunk_data.m_chunk_color_buffer = renderer.create_structured_buffer(
            (void *)&setup_chunk_data.m_chunk_color_data, sizeof(DirectX::XMFLOAT3), 1u);
    }

    setup_chunk_data.m_chunk.m_chunk_index = index;
    return setup_chunk_data;
}
void ChunkManager::add_chunk_to_setup_stack(const u64 index)
{
    if (m_loaded_chunks.contains(index) || m_unloaded_chunks.contains(index) ||
        m_chunk_indices_that_are_being_setup.contains(index))
    {
        return;
    }

    m_chunk_indices_that_are_being_setup[index] = index;
    m_chunks_to_setup_stack.push(index);
}

void ChunkManager::create_chunks_from_setup_stack(Renderer &renderer)
{
    u64 chunks_that_are_setup = 0u;
    while (chunks_that_are_setup++ < ChunkManager::NUMBER_OF_CHUNKS_TO_CREATE_PER_FRAME &&
           !m_chunks_to_setup_stack.empty())
    {
        const auto top = m_chunks_to_setup_stack.top();
        m_chunks_to_setup_stack.pop();

        m_setup_chunk_futures_queue.emplace(
            std::pair{renderer.m_copy_queue.m_monotonic_fence_value + 1,
                      std::async(&ChunkManager::internal_mt_setup_chunk, this, std::ref(renderer), top)});
    }
}

void ChunkManager::transfer_chunks_from_setup_to_loaded_state(const u64 current_copy_queue_fence_value)
{
    using namespace std::chrono_literals;

    u64 chunks_loaded = 0u;
    while (!m_setup_chunk_futures_queue.empty() && chunks_loaded < ChunkManager::NUMBER_OF_CHUNKS_TO_LOAD_PER_FRAME)
    {
        auto &setup_chunk_data = m_setup_chunk_futures_queue.front();
        if (!setup_chunk_data.second.valid())
        {
            return;
        }

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
                SetupChunkData chunk_to_load = setup_chunk_data.second.get();

                const size_t num_vertices = chunk_to_load.m_chunk_position_data.size();
                const size_t chunk_index = chunk_to_load.m_chunk.m_chunk_index;

                m_loaded_chunks[chunk_index] = std::move(chunk_to_load.m_chunk);

                m_chunk_position_buffers[chunk_index] = std::move(chunk_to_load.m_chunk_position_buffer);

                m_chunk_color_buffers[chunk_index] = std::move(chunk_to_load.m_chunk_color_buffer);

                m_chunk_number_of_vertices[chunk_index] = num_vertices;

                m_setup_chunk_futures_queue.pop();
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
