#include "game/World.hpp"

#include <queue>
#include <vector>   // for empty vertex lists when unloading
#include <set>      // for deduplicating dirty chunks during rebuild

// Fixed seed for now
World::World(Settings& i_settings) : settings(i_settings), noise(1337) {
    for (int x = -10; x < 10; ++x) {
        for (int y = -10; y < 10; ++y) {
            Chunk &chunk = emplaceChunk(x, y);
            chunk.world = this;
            chunk.GenerateSimpleTerrain(noise);
            chunk.BuildMesh(*this);
        }
    }
    running = true;
    meshThread = std::thread(&World::meshWorker, this);
}

World::~World() {
    // Cleanup if necessary & world saving?
    running = false;
    taskQueueCV.notify_all();
    if (meshThread.joinable()) meshThread.join();
}

Chunk& World::emplaceChunk(int cx, int cy) {
    // Exclusive write lock, because we modify the chunk map
    std::unique_lock<std::shared_mutex> lock(chunksMutex);

    ChunkColumn& column = chunks[cx];
    auto [it, inserted] = column.try_emplace(
        cy,
        std::make_shared<Chunk>(cx, cy)
    );
    // Ensure the new chunk knows its world
    it->second->world = this;
    return *it->second;
}

// Set or replace a chunk at position (cx, cy)
void World::setChunk(int cx, int cy, Chunk& chunk) {
    std::unique_lock<std::shared_mutex> lock(chunksMutex);

    ChunkColumn& column = chunks[cx];
    column[cy] = std::make_shared<Chunk>(chunk);
    column[cy]->world = this;
}

// Get pointer to chunk, or nullptr if it does not exist
Chunk* World::getChunk(const int cx, const int cy) {
    std::shared_lock<std::shared_mutex> lock(chunksMutex);

    auto itX = chunks.find(cx);
    if (itX == chunks.end()) return nullptr;
    auto itY = itX->second.find(cy);
    if (itY == itX->second.end()) return nullptr;
    return itY->second.get();
}

// Check whether a chunk at (cx, cy) exists/is loaded
bool World::hasChunk(const int cx, const int cy) const {
    std::shared_lock<std::shared_mutex> lock(chunksMutex);

    auto itX = chunks.find(cx);
    if (itX == chunks.end()) return false;
    auto itY = itX->second.find(cy);
    return itY != itX->second.end();
}

float distanceSq(const int x1, const int y1, const int x2, const int y2) {
    return static_cast<float>((x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2));
}

struct BlockInChunk {
    int bx, by, bz;
    Chunk* chunk;
};

BlockInChunk getBlockInChunk(World& world, const int wx, const int wy, const int wz) {
    const int cx = (wx >= 0) ? (wx / CHUNK_WIDTH) : ((wx - (CHUNK_WIDTH - 1)) / CHUNK_WIDTH);
    const int cy = (wz >= 0) ? (wz / CHUNK_WIDTH) : ((wz - (CHUNK_WIDTH - 1)) / CHUNK_WIDTH);

    int bx = wx % CHUNK_WIDTH;
    if (bx < 0) bx += CHUNK_WIDTH;
    int bz = wz % CHUNK_WIDTH;
    if (bz < 0) bz += CHUNK_WIDTH;

    if (wy < 0 || wy >= CHUNK_HEIGHT) {
        return {wx, wy, wz, nullptr};
    }

    Chunk* chunk = world.getChunk(cx, cy);
    return {bx, wy, bz, chunk};
}

BlockState World::getBlock(const int wx, const int wy, const int wz) {
    const auto [bx, by, bz, chunk] = getBlockInChunk(*this, wx, wy, wz);
    if (!chunk) {
        return BlockState(BlockType::Air);
    }

    return chunk->GetBlock(bx, by, bz);
}

bool World::setBlock(const int wx, const int wy, const int wz, const BlockType block) {
    const auto [bx, by, bz, chunk] = getBlockInChunk(*this, wx, wy, wz);
    if (!chunk) {
        return false;
    }

    // Apply block change
    chunk->SetBlock(bx, by, bz, BlockState::getBasic(block));

    // Determine the owning chunk coordinates from the chunk itself
    const int cx = chunk->cx;
    const int cz = chunk->cz;

    // Always rebuild the current chunk
    rebuildChunk(cx, cz);

    if (bx == 0) {
        rebuildChunk(cx - 1, cz); // neighbor to the -X side
    } else if (bx == CHUNK_WIDTH - 1) {
        rebuildChunk(cx + 1, cz); // neighbor to the +X side
    }

    if (bz == 0) {
        rebuildChunk(cx, cz - 1); // neighbor to the -Z side
    } else if (bz == CHUNK_WIDTH - 1) {
        rebuildChunk(cx, cz + 1); // neighbor to the +Z side
    }

    return true;
}


void World::tick() {
    //if (!player) return;

    // Player Chunk Coordinates
    const int pChunkX = static_cast<int>(std::floor(player->Position.x / CHUNK_WIDTH));
    const int pChunkZ = static_cast<int>(std::floor(player->Position.z / CHUNK_WIDTH));

    const int renderRadius = static_cast<int>(settings.GLTo / CHUNK_WIDTH) + 1;

    // Squared distance for comparisons to avoid sqrt operations
    const auto loadRadiusSq = static_cast<float>(renderRadius * renderRadius);
    // Unload radius should be slightly larger to prevent flickering (hysteresis)
    const auto unloadRadiusSq = static_cast<float>((renderRadius + 2) * (renderRadius + 2));

    // --- 3. UNLOADING CHUNKS ---
    {
        // Exclusive write lock, because we are removing chunks
        std::unique_lock<std::shared_mutex> lock(chunksMutex);

        // Iterate through all loaded chunks and remove those too far away
        for (auto itX = chunks.begin(); itX != chunks.end(); ) {
            const int cx = itX->first;
            auto& innerMap = itX->second;

            for (auto itY = innerMap.begin(); itY != innerMap.end(); ) {
                const int cy = itY->first;

                const float dist = distanceSq(cx, cy, pChunkX, pChunkZ);

                // Remove chunks on regeneration or if they exceed the unload radius
                if (noise.regen || dist > unloadRadiusSq) {
                    // Clear the mesh in the renderer so the chunk is no longer rendered
                    chunkRenderer->RemoveMesh(cx, cy);

                    itY = innerMap.erase(itY);
                } else {
                    ++itY;
                }
            }

            // If the column is empty, remove it as well
            if (innerMap.empty()) {
                itX = chunks.erase(itX);
            } else {
                ++itX;
            }
        }
    }
    noise.regen = false;

    // --- LOADING CHUNKS ---
    // We only want to generate limited chunks per tick to keep FPS high.
    int chunksGeneratedThisTick = 0;
    constexpr int MAX_GEN_PER_TICK = 999;

    bool stopLoading = false;

    // Gen chunks in a spiral pattern from the player outward
    std::queue<Chunk*> queuedChunks {};
    for (int d = 0; d <= renderRadius && !stopLoading; ++d) {
        for (int x = pChunkX - d; x <= pChunkX + d; ++x) {
            for (int z = pChunkZ - d; z <= pChunkZ + d; ++z) {

                // Check if this specific chunk is within the circular radius
                if (distanceSq(x, z, pChunkX, pChunkZ) > loadRadiusSq) continue;

                // If chunk does not exist
                if (!hasChunk(x, z)) {
                    Chunk& newChunk = emplaceChunk(x, z);
                    newChunk.GenerateSimpleTerrain(noise);
                    queuedChunks.push(&newChunk);

                    chunksGeneratedThisTick++;

                    if (chunksGeneratedThisTick >= MAX_GEN_PER_TICK) {
                        stopLoading = true;
                        break;
                    }
                }
            }
            if (stopLoading) break;
        }
    }

    // Build meshes after generation for better culling
    // Collect all chunks that need a rebuild (self + neighbors) into a set
    // to avoid rebuilding the same chunk multiple times within one tick.
    std::set<std::pair<int, int>> dirtyChunks;

    while (!queuedChunks.empty()) {
        Chunk* chunk = queuedChunks.front();
        queuedChunks.pop();

        const int cx = chunk->cx;
        const int cz = chunk->cz;

        dirtyChunks.insert({cx, cz});
        dirtyChunks.insert({cx + 1, cz});
        dirtyChunks.insert({cx - 1, cz});
        dirtyChunks.insert({cx, cz + 1});
        dirtyChunks.insert({cx, cz - 1});
    }

    for (const auto& [cx, cz] : dirtyChunks) {
        rebuildChunk(cx, cz);
    }

    uploadFinishedMeshes();
}

void World::rebuildChunk(int cx, int cy) {
    enqueueRebuildTask(cx, cy);
}

void World::rebuildChunkAndNeighbors(int cx, int cy) {
    rebuildChunk(cx, cy);
    rebuildChunk(cx + 1, cy);
    rebuildChunk(cx - 1, cy);
    rebuildChunk(cx, cy + 1);
    rebuildChunk(cx, cy - 1);
}

void World::enqueueRebuildTask(int cx, int cy) {
    // Retrieve ChunkPtr from the chunk map (as shared_ptr), ensuring the chunk
    // stays alive even if it gets removed from the map
    ChunkPtr c;

    {
        std::shared_lock<std::shared_mutex> lock(chunksMutex);

        auto itX = chunks.find(cx);
        if (itX == chunks.end()) return;
        auto itY = itX->second.find(cy);
        if (itY == itX->second.end()) return;

        c = itY->second;
    }

    if (!c) return;

    {
        std::lock_guard<std::mutex> lock(taskQueueMutex);
        taskQueue.push({cx, cy, c});
    }
    taskQueueCV.notify_one();
}

void World::meshWorker() {
    while (running) {
        MeshTask task;

        {
            std::unique_lock<std::mutex> lock(taskQueueMutex);
            taskQueueCV.wait(lock, [&]{ return !taskQueue.empty() || !running; });
            if (!running) return;

            task = taskQueue.front();
            taskQueue.pop();
        }

        task.chunk->BuildMesh(*this);

        {
            std::lock_guard<std::mutex> lock(doneQueueMutex);
            doneQueue.push(task);
        }
    }
}

void World::uploadFinishedMeshes() {
    while (true) {
        MeshTask task;

        {
            std::lock_guard<std::mutex> lock(doneQueueMutex);
            if (doneQueue.empty()) break;
            task = doneQueue.front();
            doneQueue.pop();
        }

        // Only upload if the chunk still exists
        if (chunkRenderer && hasChunk(task.cx, task.cy)) {
            chunkRenderer->UploadMesh(task.cx, task.cy, task.chunk->GetMeshVertices());
        }
    }
}