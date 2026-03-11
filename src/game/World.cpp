#include "game/World.hpp"
#include "render/gl/GLSplatRenderer.hpp"

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
}

World::~World() {
    // Cleanup if necessary & world saving?
}

Chunk& World::emplaceChunk(int cx, int cy) {
    ChunkColumn& column = chunks[cx]; // default-constructs the inner map
    auto [it, inserted] = column.try_emplace(
        cy,
        cx, cy   // forwarded to Chunk constructor
    );
    return it->second;
}

// Set or replace a chunk at position (cx, cy)
void World::setChunk(int cx, int cy, Chunk& chunk) {
    ChunkColumn& column = chunks[cx];
    auto it = column.find(cy);
    if (it != column.end()) {
        it->second = chunk;              // overwrites existing
    } else {
        column.emplace(cy, chunk);    // copy-constructs from `chunk`
    }
}

// Get pointer to chunk, or nullptr if it does not exist
Chunk* World::getChunk(const int cx, const int cy) {
    auto itX = chunks.find(cx);
    if (itX == chunks.end()) return nullptr;
    auto itY = itX->second.find(cy);
    if (itY == itX->second.end()) return nullptr;
    return &itY->second;
}

// Check whether a chunk at (cx, cy) exists/is loaded
bool World::hasChunk(const int cx, const int cy) const {
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

    // Update this chunk
    chunk->BuildMesh(*this);
    if (chunkRenderer) {
        chunkRenderer->UploadMesh(chunk->cx, chunk->cz, chunk->GetMeshVertices());
        chunkRenderer->UploadGrassMesh(chunk->cx, chunk->cz, chunk->GetGrassVertices());
    }

    // Rebuild neighboring chunks if the changed block touches a border.
    const bool atMinX = (bx == 0);
    const bool atMaxX = (bx == CHUNK_WIDTH - 1);
    const bool atMinZ = (bz == 0);
    const bool atMaxZ = (bz == CHUNK_WIDTH - 1);

    if (atMinX) rebuildChunk(chunk->cx - 1, chunk->cz);
    if (atMaxX) rebuildChunk(chunk->cx + 1, chunk->cz);
    if (atMinZ) rebuildChunk(chunk->cx, chunk->cz - 1);
    if (atMaxZ) rebuildChunk(chunk->cx, chunk->cz + 1);

    return true;
}


void World::tick() {
    if (!player) return;

    // Player Chunk Coordinates
    const int pChunkX = static_cast<int>(std::floor(player->Position.x / CHUNK_WIDTH));
    const int pChunkZ = static_cast<int>(std::floor(player->Position.z / CHUNK_WIDTH));

    const int renderRadius = static_cast<int>(settings.GLTo / CHUNK_WIDTH) + 1;

    // Squared distance for comparisons to avoid sqrt operations
    const auto loadRadiusSq = static_cast<float>(renderRadius * renderRadius);
    // Unload radius should be slightly larger to prevent flickering (hysteresis)
    const auto unloadRadiusSq = static_cast<float>((renderRadius + 2) * (renderRadius + 2));

    // --- 3. UNLOADING CHUNKS ---
    if (false) { // DEBUG
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
                    if (chunkRenderer) {
                        chunkRenderer->RemoveMesh(cx, cy);
                    }
                    if (splatRenderer) {
                        splatRenderer->RemoveSplats(cx, cy);
                    }

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
    constexpr int MAX_GEN_PER_TICK = 9;

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
}

void World::rebuildChunk(int cx, int cy) {
    //enqueueRebuildTask(cx, cy);
    Chunk* chunk = getChunk(cx, cy);
    if (!chunk || !chunkRenderer) {
        return;
    }
    chunk->BuildMesh(*this);
    chunkRenderer->UploadMesh(cx, cy, chunk->GetMeshVertices());
    chunkRenderer->UploadGrassMesh(cx, cy, chunk->GetGrassVertices());
}

void World::rebuildChunkAndNeighbors(int cx, int cy) {
    rebuildChunk(cx, cy);
    rebuildChunk(cx + 1, cy);
    rebuildChunk(cx - 1, cy);
    rebuildChunk(cx, cy + 1);
    rebuildChunk(cx, cy - 1);
}
