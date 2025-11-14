#include "game/World.hpp"

World::World() {
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
