#pragma once

#include "voxel-engine/renderer.hpp"

// A voxel is just a value on a regular 3D grid. Think of it as the corners where the cells meet in a 3d grid.
// For 3d visualization of voxels, A cube is rendered for each voxel where the front lower left corner is the 'voxel
// position' and has a edge length as specified in the class below.
struct Voxel
{
    static constexpr u32 EDGE_LENGTH{64u};
    bool m_active{true};
};

struct Chunk
{
    explicit Chunk();

    Chunk(const Chunk &other) = delete;
    Chunk &operator=(Chunk &other) = delete;

    Chunk(Chunk &&other) noexcept;
    Chunk &operator=(Chunk &&other) noexcept;

    ~Chunk();

    static constexpr u32 NUMBER_OF_VOXELS_PER_DIMENSION = 8u;
    static constexpr size_t NUMBER_OF_VOXELS =
        NUMBER_OF_VOXELS_PER_DIMENSION * NUMBER_OF_VOXELS_PER_DIMENSION * NUMBER_OF_VOXELS_PER_DIMENSION;

    static constexpr u32 CHUNK_LENGTH = Voxel::EDGE_LENGTH * Chunk::NUMBER_OF_VOXELS_PER_DIMENSION;

    // A flattened 3d array of Voxels.
    Voxel *m_voxels{};
    size_t m_chunk_index{};
};

// A class that contains a collection of chunks and associated data.
// The states a chunk can be in:
// (i) Loaded -> Ready to be rendered.
// (ii) Setup -> Chunk mesh is ready, but associated buffers may or maynot be ready. Once the buffers are ready, these
// chunks are moved into the loaded chunks hashmap.
struct ChunkManager
{
    // Constructor creates the shared position buffer.
    explicit ChunkManager(Renderer &renderer);

    struct SetupChunkData
    {
        Chunk m_chunk{};

        Renderer::IndexBufferWithIntermediateResource m_chunk_index_buffer{};
        Renderer::StucturedBufferWithIntermediateResource m_chunk_color_buffer{};

        // A strange design decision, but rather than accessing the render resources via root constants, render
        // resources will now be embedded into the chunk constant buffer.
        // This is done to make the indirect rendering & GPU culling process simpler.
        ConstantBuffer m_chunk_constant_buffer{};

        std::vector<u16> m_chunk_indices_data{};
        std::vector<DirectX::XMFLOAT3> m_chunk_color_data{};
    };

  private:
    // internal_mt : Internal multithreaded.
    SetupChunkData internal_mt_setup_chunk(Renderer &renderer, const size_t index);

  public:
    void add_chunk_to_setup_stack(const u64 chunk_index);
    void create_chunks_from_setup_stack(Renderer &renderer);

    void transfer_chunks_from_setup_to_loaded_state(const u64 current_copy_queue_fence_value);

    static constexpr u32 NUMBER_OF_CHUNKS_PER_DIMENSION = 2048u;
    static constexpr size_t NUMBER_OF_CHUNKS =
        NUMBER_OF_CHUNKS_PER_DIMENSION * NUMBER_OF_CHUNKS_PER_DIMENSION * NUMBER_OF_CHUNKS_PER_DIMENSION;

    // Determines how many chunks are loaded around the player.
    static constexpr u32 CHUNKS_LOADED_AROUND_PLAYER = 6u;

    // Determines how many chunks are deleted per frame.
    static constexpr u32 CHUNKS_TO_UNLOAD_PER_FRAME = 64u * 4u;

    // Determine how many chunks can be loaded at once. If a chunk is to be loaded and loaded chunks is already at the
    // limit, re-use of memory happens.
    static constexpr u32 CHUNK_RENDER_DISTANCE = CHUNKS_LOADED_AROUND_PLAYER;

    // Chunks to create per frame : How many chunks are setup (i.e the meshing processes occurs).
    static constexpr u32 NUMBER_OF_CHUNKS_TO_CREATE_PER_FRAME = 16u;

    // Chunks to load per frame : How many setup chunks are moved into the loaded chunk hash map.
    static constexpr u32 NUMBER_OF_CHUNKS_TO_LOAD_PER_FRAME = 64u;

    std::unordered_map<size_t, Chunk> m_loaded_chunks{};

    // NOTE : Chunks are considered to be setup when :
    // (i) The result of async call (i.e the future) is ready,
    // (ii) The fence value is < the current copy queue fence value.
    // The setup chunks future stack consist of pairs of fence values , futures.
    std::queue<std::pair<u64, std::future<SetupChunkData>>> m_setup_chunk_futures_queue{};

    // Why is there also a stack?
    // Use the stack to store chunk indices that at any given point in time are close to the player.
    // Then, each from from this stack, add elements into the queue.
    std::stack<u64> m_chunks_to_setup_stack{};

    // A unordered set to keep track of chunks that are currently in process of being setup.
    // This is required in case create_chunk is called for a chunk that is being setup but not loaded. We do not want to
    // load this chunk again.
    std::unordered_set<size_t> m_chunk_indices_that_are_being_setup{};

    std::unordered_map<size_t, IndexBuffer> m_chunk_index_buffers{};
    std::unordered_map<size_t, StructuredBuffer> m_chunk_color_buffers{};
    std::unordered_map<size_t, ConstantBuffer> m_chunk_constant_buffers{};

    // All chunks only have a index buffer with them. The indices 'index' into this common shared chunk constant buffer.
    // The data in this buffer is ordered vertex wise, voxel wise.
    StructuredBuffer m_shared_chunk_position_buffer{};
};
