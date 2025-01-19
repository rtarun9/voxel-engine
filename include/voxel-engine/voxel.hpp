#pragma once

#include "shaders/interop/render_resources.hlsli"

#include "voxel-engine/rhi/renderer.hpp"
#include "voxel-engine/thread_pool.hpp"

// A voxel is just a value on a regular 3D grid. Think of it as the corners where the cells meet in a 3d grid.
// For 3d visualization of voxels, A cube is rendered for each voxel where the front lower left corner is the 'voxel
// position' and has a edge length as specified in the class below.
struct voxel_t
{
    static constexpr u32 EDGE_LENGTH{64u};
    b32 m_active : 1 = 1;
};

struct voxel_chunk_position_t
{
    i32 x{};
    i32 y{};
    i32 z{};

    b32 operator==(const voxel_chunk_position_t &other) const
    {
        return x == other.x && y == other.y && z == other.z;
    }

    std::wstring to_wstring() const
    {
        return std::to_wstring(x) + L"," + std::to_wstring(y) + L"," + std::to_wstring(z);
    }
};

// Specializing std::hash for voxel_chunk_position_t
namespace std
{
template <> struct hash<voxel_chunk_position_t>
{
    size_t operator()(const voxel_chunk_position_t &pos) const
    {
        // Combine the hash of each member
        return (hash<int>()(pos.x) ^ (hash<int>()(pos.y) << 1)) ^ (hash<int>()(pos.z) << 2);
    }
};
} // namespace std

// Each chunk has offsets into index buffer and color buffers that are stored in the chunk manager.
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

    size_t m_index_buffer_start_index_location{};
    size_t m_color_buffer_start_index_location{};

    std::vector<u16> m_index_buffer_data{};
    DirectX::XMFLOAT3 m_color_buffer_data{};

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
    explicit voxel_chunk_manager_t(rhi::renderer_t &renderer);

  public:
    void add_chunk_to_setup_stack(const voxel_chunk_position_t chunk_position);
    void create_chunks_from_setup_stack(rhi::renderer_t &renderer);

    void transfer_chunks_from_setup_to_loaded_state();

    // Chunks to create per frame : How many chunks are setup (i.e the meshing processes occurs).
    static constexpr u32 NUMBER_OF_CHUNKS_TO_CREATE_PER_FRAME = 16u;

    // Chunks to load per frame : How many setup chunks are moved into the loaded chunk hash map.
    static constexpr u32 NUMBER_OF_CHUNKS_TO_LOAD_PER_FRAME = 16u;

    std::unordered_map<voxel_chunk_position_t, voxel_chunk_t> m_loaded_chunks{};

    std::queue<std::future<voxel_chunk_t>> m_setup_chunk_futures_queue{};

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

    rhi::upload_structured_buffer_t m_color_buffer{};
    rhi::upload_structured_buffer_t m_index_buffer{};

    // A queue of offsets into the chunk managers's color and index buffer.
    // To be used when new chunks are being created. When old chunks are deleted, their values must be passed into this.
    struct chunk_manager_buffer_offset_t
    {
        size_t m_color_buffer_start_index_location{};
        size_t m_index_buffer_start_index_location{};
    };

    std::queue<chunk_manager_buffer_offset_t> m_chunk_manager_buffer_offset_queue{};

    // Threadpool from which std::futures are obtained.
    thread_pool_t m_thread_pool{};
};
