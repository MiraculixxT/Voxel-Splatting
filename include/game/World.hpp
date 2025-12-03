#pragma once

#include <condition_variable>
#include <memory>
#include <shared_mutex>
#include <atomic>

#include "Chunk.hpp"
#include "Player.hpp"
#include "Settings.hpp"
#include "render/gl/GLChunkRenderer.hpp"
#include "queue"
#include "mutex"
#include "thread"
#include "WorldGen.hpp"


class World {
public:
    explicit World(Settings& i_settings);
    ~World();

    // map[x][y] -> shared_ptr<Chunk>
    using ChunkPtr     = std::shared_ptr<Chunk>;
    using ChunkColumn  = std::unordered_map<int, ChunkPtr>;
    using ChunkStorage = std::unordered_map<int, ChunkColumn>;

    // construct chunk in-place from coords
    Chunk& emplaceChunk(int cx, int cy);

    // add or replace a chunk at (cx, cy)
    void setChunk(int cx, int cy, Chunk& chunk);

    // get pointer to chunk, or nullptr if it does not exist
    Chunk* getChunk(int cx, int cy);

    // check if chunk at (cx, cy) exists/is loaded
    bool hasChunk(int cx, int cy) const;

    const ChunkStorage& getChunks() const { return chunks; }
    ChunkStorage& getChunks() { return chunks; }

    BlockState getBlock(int wx, int wy, int wz);
    bool setBlock(int wx, int wy, int wz, BlockType block);

    void setPlayer(Player* i_player) { player = i_player; }
    void setChunkRenderer(GLChunkRenderer* i_chunkRenderer) { chunkRenderer = i_chunkRenderer; }
    TerrainNoise& getTerrainNoise() { return noise; }

    void tick();

    void rebuildChunk(int cx, int cy);
    void rebuildChunkAndNeighbors(int cx, int cy);

    // Async
    void enqueueRebuildTask(int cx, int cy);
    void uploadFinishedMeshes();
    void meshWorker();

private:
    ChunkStorage chunks;
    mutable std::shared_mutex chunksMutex;
    Settings& settings;
    Player* player = nullptr;

    GLChunkRenderer* chunkRenderer = nullptr;

    // Async meshing system
    struct MeshTask {
        int cx, cy;
        ChunkPtr chunk;
    };

    std::queue<MeshTask> taskQueue;
    std::queue<MeshTask> doneQueue;

    std::mutex taskQueueMutex;
    std::mutex doneQueueMutex;

    std::condition_variable taskQueueCV;

    std::thread meshThread;
    std::atomic<bool> running = false;

    TerrainNoise noise;
};
