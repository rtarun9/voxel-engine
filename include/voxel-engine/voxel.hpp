#pragma once

#include "shaders/interop/render_resources.hlsli"

#include "voxel-engine/rhi/renderer.hpp"
#include "voxel-engine/thread_pool.hpp"

// A voxel is just a value on a regular 3D grid. Think of it as the corners where the cells meet in a 3d grid.
// For 3d visualization of voxels, A cube is rendered for each voxel where the front lower left corner is the 'voxel
// position' and has a edge length as specified in the class below.
struct voxel_t
{
    static constexpr u32 EDGE_LENGTH{1u};
    b32 m_active : 1 = 1;
};

struct voxel_chunk_position_t
{
    i32 x{};
    i32 y{};
    i32 z{};
};

// Each chunk has a index buffer and color buffer. This is because during rendering entire chunks are rendered at once.
// A shared / common position buffer is used, that is created and handled by chunk manager class.
struct voxel_chunk_t
{
    explicit voxel_chunk_t();

    voxel_chunk_t(const voxel_chunk_t &other) = delete;
    voxel_chunk_t &operator=(voxel_chunk_t &other) = delete;

    voxel_chunk_t(voxel_chunk_t &&other) noexcept;
    voxel_chunk_t &operator=(voxel_chunk_t &&other) noexcept;

    ~voxel_chunk_t() = default;

    static constexpr u32 CHUNK_LENGTH = voxel_t::EDGE_LENGTH * NUMBER_OF_VOXELS_PER_DIMENSION_IN_CHUNK;

    // A flattened 1d array of Voxels.
    std::unique_ptr<voxel_t[]> m_voxels{};

    rhi::index_buffer_t m_index_buffer{};
    rhi::structured_buffer_t m_color_buffer{};

    voxel_chunk_position_t m_chunk_position{};
};

// A class that contains a collection of chunks and associated data.
// The states a chunk can be in:
// (i) Loaded -> Ready to be rendered.
// (ii) Setup -> Chunk mesh is ready, but associated buffers may or maynot be ready. Once the buffers are ready, these
// chunks are moved into the loaded chunks hashmap.
// The class contains several hashmaps, for which the chunk position acts as a index.
struct voxel_chunk_manager_t
{
    // Constructor creates the shared position buffer.
    explicit voxel_chunk_manager_t(rhi::renderer_t &renderer);

    // Because of the async nature of copy operations, intermediate buffers need to be kept in memory until the
    // operation has completed succesfully.
    // Note that the chunk manager has a large constant buffer that is to be used by all chunks, so each chunk won't
    // have a specific constant buffer.
    struct voxel_chunk_setup_data_t
    {
        voxel_chunk_t m_chunk{};

        rhi::renderer_t::index_buffer_with_intermediate_resource_t m_chunk_index_buffer{};
        rhi::renderer_t::structured_buffer_with_intermediate_resource_t m_chunk_color_buffer{};
    };

  public:
    void add_chunk_to_setup_stack(const voxel_chunk_position_t chunk_position);
    void create_chunks_from_setup_stack(rhi::renderer_t &renderer);

    void transfer_chunks_from_setup_to_loaded_state(const u64 current_copy_queue_fence_value);

    // Chunks to create per frame : How many chunks are setup (i.e the meshing processes occurs).
    static constexpr u32 NUMBER_OF_CHUNKS_TO_CREATE_PER_FRAME = 16u;

    // Chunks to load per frame : How many setup chunks are moved into the loaded chunk hash map.
    static constexpr u32 NUMBER_OF_CHUNKS_TO_LOAD_PER_FRAME = 16u;

    std::unordered_map<voxel_chunk_position_t, voxel_chunk_t> m_loaded_chunks{};

    // NOTE : Chunks are considered to be setup when the result of async call (i.e the future) is ready.
    // The setup chunks future stack consist of pairs of {fence values , futures}.
    std::queue<std::pair<u64, std::future<voxel_chunk_setup_data_t>>> m_setup_chunk_futures_queue{};

    // Why is there also a stack?
    // Use the stack to store chunk indices that at any given point in time are close to the player.
    // Then, each from from this stack, add elements into the queue.
    std::stack<voxel_chunk_position_t> m_chunks_to_setup_stack{};

    // A unordered set to keep track of chunks that are currently in process of being setup.
    // This is required in case create_chunk is called for a chunk that is being setup but not loaded. We do not want to
    // load this chunk again.
    std::unordered_set<voxel_chunk_position_t> m_chunk_indices_that_are_being_setup{};

    // All chunks only have a index buffer with them. The indices 'index' into this common shared chunk constant buffer.
    // The data in this buffer is ordered vertex wise, voxel wise.
    rhi::structured_buffer_t m_shared_chunk_position_buffer{};

    // Threadpool from which std::futures are obtained.
    thread_pool_t m_thread_pool{6u};
};
