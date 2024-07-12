#include "voxel-engine/voxel.hpp"

#include "shaders/interop/render_resources.hlsli"

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

ChunkManager::ChunkManager(Renderer &renderer)
{
    // Create the position buffer.
    std::vector<DirectX::XMFLOAT3> chunk_position_data{};

    static constexpr std::array<DirectX::XMFLOAT3, 8> chunk_voxel_vertices{
        DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f),
        DirectX::XMFLOAT3(0.0f, Voxel::EDGE_LENGTH, 0.0f),
        DirectX::XMFLOAT3(Voxel::EDGE_LENGTH, Voxel::EDGE_LENGTH, 0.0f),
        DirectX::XMFLOAT3(Voxel::EDGE_LENGTH, 0.0f, 0.0f),
        DirectX::XMFLOAT3(0.0f, 0.0f, Voxel::EDGE_LENGTH),
        DirectX::XMFLOAT3(0.0f, Voxel::EDGE_LENGTH, Voxel::EDGE_LENGTH),
        DirectX::XMFLOAT3(Voxel::EDGE_LENGTH, Voxel::EDGE_LENGTH, Voxel::EDGE_LENGTH),
        DirectX::XMFLOAT3(Voxel::EDGE_LENGTH, 0.0f, Voxel::EDGE_LENGTH),
    };

    for (size_t i = 0; i < Chunk::NUMBER_OF_VOXELS; i++)
    {
        const DirectX::XMUINT3 index_3d = convert_to_3d(i, Chunk::NUMBER_OF_VOXELS_PER_DIMENSION);
        const DirectX::XMFLOAT3 offset = DirectX::XMFLOAT3(
            index_3d.x * Voxel::EDGE_LENGTH, index_3d.y * Voxel::EDGE_LENGTH, index_3d.z * Voxel::EDGE_LENGTH);

        for (const auto &vertex : chunk_voxel_vertices)
        {
            chunk_position_data.push_back({vertex.x + offset.x, vertex.y + offset.y, vertex.z + offset.z});
        }
    }

    const auto result = renderer.create_structured_buffer(chunk_position_data.data(), sizeof(DirectX::XMFLOAT3),
                                                          chunk_position_data.size(), L"shared chunk position buffer");

    renderer.m_copy_queue.flush_queue();
    m_shared_chunk_position_buffer = result.structured_buffer;

    m_thread_pool.reset(2);
}

ChunkManager::SetupChunkData ChunkManager::internal_mt_setup_chunk(Renderer &renderer, const size_t index)
{
    SetupChunkData setup_chunk_data{};

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

    for (size_t i = 0; i < Chunk::NUMBER_OF_VOXELS; i++)
    {
        if (!setup_chunk_data.m_chunk.m_voxels[i].m_active)
        {
            continue;
        }

        const auto voxel_color = chunk_color;

        const DirectX::XMUINT3 index_3d = convert_to_3d(i, Chunk::NUMBER_OF_VOXELS_PER_DIMENSION);
        const u16 shared_index_buffer_offset = i * 8u;

        // Check if there is a voxel that blocks the front face of current voxel.
        {

            const bool is_front_face_covered =
                (index_3d.z != 0 && setup_chunk_data.m_chunk
                                        .m_voxels[convert_to_1d({index_3d.x, index_3d.y, index_3d.z - 1},
                                                                Chunk::NUMBER_OF_VOXELS_PER_DIMENSION)]
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

            const bool is_back_face_covered = (index_3d.z != Chunk::NUMBER_OF_VOXELS_PER_DIMENSION - 1u &&
                                               setup_chunk_data.m_chunk
                                                   .m_voxels[convert_to_1d({index_3d.x, index_3d.y, index_3d.z + 1},
                                                                           Chunk::NUMBER_OF_VOXELS_PER_DIMENSION)]
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
                (index_3d.x != 0u && setup_chunk_data.m_chunk
                                         .m_voxels[convert_to_1d({index_3d.x - 1, index_3d.y, index_3d.z},
                                                                 Chunk::NUMBER_OF_VOXELS_PER_DIMENSION)]
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

            const bool is_right_face_covered = (index_3d.x != Chunk::NUMBER_OF_VOXELS_PER_DIMENSION - 1u &&
                                                setup_chunk_data.m_chunk
                                                    .m_voxels[convert_to_1d({index_3d.x + 1, index_3d.y, index_3d.z},
                                                                            Chunk::NUMBER_OF_VOXELS_PER_DIMENSION)]
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

            const bool is_top_face_covered = (index_3d.y != Chunk::NUMBER_OF_VOXELS_PER_DIMENSION - 1 &&
                                              setup_chunk_data.m_chunk
                                                  .m_voxels[convert_to_1d({index_3d.x, index_3d.y + 1, index_3d.z},
                                                                          Chunk::NUMBER_OF_VOXELS_PER_DIMENSION)]
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
                (index_3d.y != 0u && setup_chunk_data.m_chunk
                                         .m_voxels[convert_to_1d({index_3d.x, index_3d.y - 1, index_3d.z},
                                                                 Chunk::NUMBER_OF_VOXELS_PER_DIMENSION)]
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

    setup_chunk_data.m_chunk_indices_data = std::move(chunk_index_data);
    setup_chunk_data.m_chunk_color_data = std::move(color_data);

    if (!setup_chunk_data.m_chunk_indices_data.empty())
    {

        setup_chunk_data.m_chunk_index_buffer =
            renderer.create_index_buffer((void *)setup_chunk_data.m_chunk_indices_data.data(), sizeof(u16),
                                         setup_chunk_data.m_chunk_indices_data.size(),
                                         std::wstring(L"Chunk Index buffer : ") + std::to_wstring(index));
        setup_chunk_data.m_chunk_color_buffer =
            renderer.create_structured_buffer((void *)setup_chunk_data.m_chunk_color_data.data(),
                                              sizeof(DirectX::XMFLOAT3), setup_chunk_data.m_chunk_color_data.size(),
                                              std::wstring(L"Chunk color buffer : ") + std::to_wstring(index));

        setup_chunk_data.m_chunk_constant_buffer = renderer.create_constant_buffer<1>(
            sizeof(ChunkConstantBuffer), std::wstring(L"Chunk constant buffer : ") + std::to_wstring(index))[0];
    }

    setup_chunk_data.m_chunk.m_chunk_index = index;
    return setup_chunk_data;
}

void ChunkManager::add_chunk_to_setup_stack(const u64 index)
{
    if (m_loaded_chunks.contains(index) || m_chunk_indices_that_are_being_setup.contains(index))
    {
        return;
    }

    m_chunk_indices_that_are_being_setup.insert(index);
    m_chunks_to_setup_stack.push(index);
}

void ChunkManager::create_chunks_from_setup_stack(Renderer &renderer)
{
    u64 chunks_that_are_setup = 0u;
    while (chunks_that_are_setup++ < ChunkManager::NUMBER_OF_CHUNKS_TO_CREATE_PER_FRAME &&
           !m_chunks_to_setup_stack.empty())
    {
        const size_t top = m_chunks_to_setup_stack.top();
        m_chunks_to_setup_stack.pop();

        m_setup_chunk_futures_queue.emplace(std::pair{
            renderer.m_copy_queue.m_monotonic_fence_value + 1,
            m_thread_pool.submit_task([this, &renderer, top]() { return internal_mt_setup_chunk(renderer, top); })});
    }
}

void ChunkManager::transfer_chunks_from_setup_to_loaded_state(const u64 current_copy_queue_fence_value)
{
    using namespace std::chrono_literals;

    u64 chunks_loaded = 0u;
    while (!m_setup_chunk_futures_queue.empty() && chunks_loaded < ChunkManager::NUMBER_OF_CHUNKS_TO_LOAD_PER_FRAME)
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
                SetupChunkData chunk_to_load = setup_chunk_data.second.get();

                const size_t num_vertices = chunk_to_load.m_chunk_indices_data.size();
                const size_t chunk_index = chunk_to_load.m_chunk.m_chunk_index;

                m_chunk_index_buffers[chunk_index] = std::move(chunk_to_load.m_chunk_index_buffer.index_buffer);
                m_chunk_color_buffers[chunk_index] = std::move(chunk_to_load.m_chunk_color_buffer.structured_buffer);
                m_chunk_constant_buffers[chunk_index] = std::move(chunk_to_load.m_chunk_constant_buffer);

                const DirectX::XMUINT3 chunk_index_3d =
                    convert_to_3d(chunk_index, ChunkManager::NUMBER_OF_CHUNKS_PER_DIMENSION);

                const DirectX::XMUINT3 chunk_offset =
                    DirectX::XMUINT3(chunk_index_3d.x * Voxel::EDGE_LENGTH * Chunk::NUMBER_OF_VOXELS_PER_DIMENSION,
                                     chunk_index_3d.y * Voxel::EDGE_LENGTH * Chunk::NUMBER_OF_VOXELS_PER_DIMENSION,
                                     chunk_index_3d.z * Voxel::EDGE_LENGTH * Chunk::NUMBER_OF_VOXELS_PER_DIMENSION);

                const ChunkConstantBuffer chunk_constant_buffer_data = {
                    .translation_vector = {chunk_offset.x, chunk_offset.y, chunk_offset.z, 0u},
                    .position_buffer_index = static_cast<u32>(m_shared_chunk_position_buffer.srv_index),
                    .color_buffer_index = static_cast<u32>(m_chunk_color_buffers[chunk_index].srv_index),
                };

                m_chunk_constant_buffers[chunk_index].update(&chunk_constant_buffer_data);

                m_setup_chunk_futures_queue.pop();

                m_chunk_indices_that_are_being_setup.erase(chunk_index);
                m_loaded_chunks[chunk_index] = std::move(chunk_to_load.m_chunk);
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
