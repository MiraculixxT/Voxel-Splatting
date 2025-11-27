#include "game/World.hpp"

#include <cstdio>

#include "game/ChunkManager.hpp"
#include "render/gl/GLWorldRenderer.hpp"

World::World(Settings& i_settings, GLWorldRenderer& i_gl_world_renderer): settings(i_settings), gl_world_renderer(i_gl_world_renderer) {
    // Generate spawn chunks
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
void World::setChunk(const int cx, const int cy, const Chunk& chunk) {
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

    Chunk* chunk = getChunk(cx, cy);
    if (!chunk) {
        return BlockState(BlockType::Air);
    }

    return chunk->GetBlock(bx, wy, bz);
}

void World::setPlayer(Player *pl) {
    player = pl;
}

// Helper for squared distance
float distanceSq(const int x1, const int y1, const int x2, const int y2) {
    return static_cast<float>((x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2));
}

void World::tick() {
    if (!player) return;

    // Get Player Chunk Coordinates
    const int pChunkX = static_cast<int>(std::floor(player->Position.x / CHUNK_WIDTH));
    const int pChunkZ = static_cast<int>(std::floor(player->Position.z / CHUNK_WIDTH));

    // Calculate Render Radius (in Chunks)
    const int renderRadius = static_cast<int>(settings.GLTo / CHUNK_WIDTH) + 1;

    const auto loadRadiusSq = static_cast<float>(renderRadius * renderRadius);
    // Unload radius should be slightly larger to prevent flickering
    const auto unloadRadiusSq = static_cast<float>((renderRadius + 2) * (renderRadius + 2));

    // --- UNLOADING CHUNKS ---
    // Iterate all loaded chunks and remove those too far away
    for (auto itX = chunks.begin(); itX != chunks.end(); ) {
        break; // Temporary disable unloading for testing
        const int cx = itX->first;
        auto& innerMap = itX->second;

        for (auto itY = innerMap.begin(); itY != innerMap.end(); ) {
            const int cy = itY->first;

            const float dist = distanceSq(cx, cy, pChunkX, pChunkZ);

            if (dist > unloadRadiusSq) {
                // For now, we simply erase to free memory.
                itY = innerMap.erase(itY);
                printf("Chunk Remove: %i %i\n", pChunkX, pChunkZ);
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
    Chunk* readyChunk = nullptr;
    int uploads = 0;

    while (uploads < 2 && chunk_manager.GetFinishedChunk(readyChunk)) {
        // Add to World Map
        printf(" - Finished Chunk: %i %i\n", readyChunk->cx, readyChunk->cz);
        chunks[readyChunk->cx].emplace(readyChunk->cz, std::move(*readyChunk));

        // Upload to GPU
        const Chunk* placedChunk = getChunk(readyChunk->cx, readyChunk->cz);
        gl_world_renderer.GetChunkRenderer()->UploadMesh(placedChunk->cx, placedChunk->cz, placedChunk->GetMeshVertices());

        // Cleanup the heap pointer (since we moved the data into the map)
        delete readyChunk;
        uploads++;
    }

    // Queue New Jobs
    int queued = 0;
    for (int x = pChunkX - renderRadius; x <= pChunkX + renderRadius; ++x) {
        for (int z = pChunkZ - renderRadius; z <= pChunkZ + renderRadius; ++z) {

            if (queued > 5) break; // Don't queue too many at once

            // If chunk doesn't exist AND isn't already being loaded
            bool chunkExists = hasChunk(x, z);
            bool chunkQueued = chunk_manager.IsQueued(x, z);
            printf(" - Exists: %i | Queued: %i\n", chunkExists ? 1 : 0, chunkQueued ? 1 : 0);

            if (chunkExists) {
                gl_world_renderer.GetChunkRenderer()->UploadMesh(x, z, getChunk(x, z)->GetMeshVertices());
                continue;
            }

            if (!chunk_manager.IsQueued(x, z)) {
                printf("Request Chunk: %i %i\n", x, z);
                chunk_manager.RequestChunk(x, z);
                queued++;
            }
        }
    }
}

