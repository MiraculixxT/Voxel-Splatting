#pragma once

#include <condition_variable>

#include "Chunk.hpp"
#include "FastNoiseAPI.hpp"
#include "Player.hpp"
#include "Settings.hpp"
#include "render/gl/GLChunkRenderer.hpp"
#include "queue"
#include "mutex"
#include "thread"

// Define world constants
constexpr int SEA_LEVEL = 64;
constexpr int SNOW_LEVEL = 110;

// Biome Types
enum class BiomeType {
    Ocean,
    Beach,
    Plains,
    Forest,
    Mountain
};

struct TerrainNoise {
    FastNoiseLite continental;
    FastNoiseLite moisture;
    FastNoiseLite peaks;

    explicit TerrainNoise(const int seed) {
        // --- Continentalness (The "Macro" World) ---
        // distinct large oceans and continents.
        continental.SetSeed(seed);
        continental.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        continental.SetFrequency(0.005f); // Low frequency = massive shapes
        continental.SetFractalType(FastNoiseLite::FractalType_FBm);
        continental.SetFractalOctaves(4); // Enough detail for coastlines

        // --- Moisture (Climate Zones) ---
        // Offset seed so it doesn't match height exactly
        moisture.SetSeed(seed + 1234);
        moisture.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        moisture.SetFrequency(0.008f);
        moisture.SetFractalType(FastNoiseLite::FractalType_FBm);
        moisture.SetFractalOctaves(2); // Low detail, climate changes slowly

        // --- Mountain Peaks (The "Jagged" Detail) ---
        // High frequency + Ridged fractal = Sharp cliffs
        peaks.SetSeed(seed + 4321);
        peaks.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        peaks.SetFrequency(0.015f);
        peaks.SetFractalType(FastNoiseLite::FractalType_Ridged); // mountain gen
        peaks.SetFractalOctaves(5);
    }
};

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
    void setChunk(int cx, int cy, Chunk& chunk);

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

    // Async
    void enqueueRebuildTask(int cx, int cy);
    void uploadFinishedMeshes();
    void meshWorker();

private:
    ChunkStorage chunks;
    Settings& settings;
    Player* player = nullptr;

    GLChunkRenderer* chunkRenderer = nullptr;

    // --- Async meshing system ---
    struct MeshTask {
        int cx, cy;
        Chunk* chunk;
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
