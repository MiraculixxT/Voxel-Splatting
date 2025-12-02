#pragma once

#include "Chunk.hpp"
#include "Player.hpp"
#include "Settings.hpp"
#include "render/gl/GLChunkRenderer.hpp"

class World {
public:
    explicit World(Settings& i_settings);
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

    BlockState getBlock(int wx, int wy, int wz);

    void setPlayer(Player* i_player) { player = i_player; }
    void setChunkRenderer(GLChunkRenderer* i_chunkRenderer) { chunkRenderer = i_chunkRenderer; }

    void tick();

    void rebuildChunk(int cx, int cy);
    void rebuildChunkAndNeighbors(int cx, int cy);

private:
    ChunkStorage chunks;
    Settings& settings;
    Player* player = nullptr;

    GLChunkRenderer* chunkRenderer = nullptr;
};
