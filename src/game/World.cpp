#include "game/World.hpp"

#include <queue>

World::World(Settings& i_settings): settings(i_settings) {
    for (int x = -10; x < 10; ++x) {
        for (int y = -10; y < 10; ++y) {
            Chunk& chunk = emplaceChunk(x, y);
            chunk.GenerateSimpleTerrain();
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

// add or replace a chunk at (cx, cy)
void World::setChunk(int cx, int cy, const Chunk& chunk) {
    ChunkColumn& column = chunks[cx];
    auto it = column.find(cy);
    if (it != column.end()) {
        it->second = chunk;              // overwrites existing
    } else {
        column.emplace(cy, chunk);    // copy-constructs from `chunk`
    }
}

// get pointer to chunk, or nullptr if it does not exist
Chunk* World::getChunk(const int cx, const int cy) {
    auto itX = chunks.find(cx);
    if (itX == chunks.end()) return nullptr;
    auto itY = itX->second.find(cy);
    if (itY == itX->second.end()) return nullptr;
    return &itY->second;
}

// check if chunk at (cx, cy) exists/is loaded
bool World::hasChunk(const int cx, const int cy) const {
    auto itX = chunks.find(cx);
    if (itX == chunks.end()) return false;
    auto itY = itX->second.find(cy);
    return itY != itX->second.end();
}

float distanceSq(const int x1, const int y1, const int x2, const int y2) {
    return static_cast<float>((x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2));
}

BlockState World::getBlock(const int wx, const int wy, const int wz) {
    const int cx = (wx >= 0) ? (wx / CHUNK_WIDTH) : ((wx - (CHUNK_WIDTH - 1)) / CHUNK_WIDTH);
    const int cy = (wz >= 0) ? (wz / CHUNK_WIDTH) : ((wz - (CHUNK_WIDTH - 1)) / CHUNK_WIDTH);

    int bx = wx % CHUNK_WIDTH;
    if (bx < 0) bx += CHUNK_WIDTH;
    int bz = wz % CHUNK_WIDTH;
    if (bz < 0) bz += CHUNK_WIDTH;

    if (wy < 0 || wy >= CHUNK_HEIGHT) {
        return BlockState(BlockType::Air);
    }

    const Chunk* chunk = getChunk(cx, cy);
    if (!chunk) {
        return BlockState(BlockType::Air);
    }

    return chunk->GetBlock(bx, wy, bz);
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
    // Iterate all loaded chunks and remove those too far away
    for (auto itX = chunks.begin(); itX != chunks.end(); ) {
        const int cx = itX->first;
        auto& innerMap = itX->second;

        for (auto itY = innerMap.begin(); itY != innerMap.end(); ) {
            const int cy = itY->first;

            const float dist = distanceSq(cx, cy, pChunkX, pChunkZ);

            if (dist > unloadRadiusSq) {
                // Determine if we should delete or buffer.
                // For now, we simply erase to free memory.
                itY = innerMap.erase(itY);
            } else {
                ++itY;
            }
        }

        // If the X-column is empty, remove it too
        if (innerMap.empty()) {
            itX = chunks.erase(itX);
        } else {
            ++itX;
        }
    }

    // --- LOADING CHUNKS ---
    // We only want to generate limited chunks per tick to keep FPS high.
    int chunksGeneratedThisTick = 0;
    constexpr int MAX_GEN_PER_TICK = 10;

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
                    newChunk.GenerateSimpleTerrain();
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
    while (!queuedChunks.empty()) {
        Chunk* chunk = queuedChunks.front();
        queuedChunks.pop();

        // Rebuild the new chunk and its neighbors so boundary faces get culled correctly
        rebuildChunkAndNeighbors(chunk->cx, chunk->cz);
    }
}

void World::rebuildChunk(int cx, int cy) {
    if (!chunkRenderer) return;

    Chunk* chunk = getChunk(cx, cy);
    if (!chunk) return;

    // Mesh neu aufbauen (sichtbare Faces inkl. Nachbarn)
    chunk->BuildMesh(*this);

    // An Renderer durchreichen
    chunkRenderer->UploadMesh(cx, cy, chunk->GetMeshVertices());
}

void World::rebuildChunkAndNeighbors(int cx, int cy) {
    rebuildChunk(cx, cy);
    rebuildChunk(cx + 1, cy);
    rebuildChunk(cx - 1, cy);
    rebuildChunk(cx, cy + 1);
    rebuildChunk(cx, cy - 1);
}