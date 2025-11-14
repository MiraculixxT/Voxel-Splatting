#pragma once
#include <vector>

#include "Chunk.hpp"

class World {
public:
    World();
    ~World();

    // map[x][y] -> Chunk
    using ChunkColumn  = std::unordered_map<int, Chunk>;
    using ChunkStorage = std::unordered_map<int, ChunkColumn>;

    // construct chunk in-place from coords
    Chunk& emplaceChunk(int cx, int cy);

    // add or replace a chunk at (cx, cy)
    void setChunk(int cx, int cy, const Chunk& chunk);

    // get pointer to chunk, or nullptr if it does not exist
    Chunk* getChunk(int cx, int cy);

    // check if chunk at (cx, cy) exists/is loaded
    bool hasChunk(int cx, int cy) const;

    const ChunkStorage& getChunks() const { return chunks; }
    ChunkStorage& getChunks() { return chunks; }


private:
    ChunkStorage chunks;
};
