#pragma once
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <unordered_set>

#include "Chunk.hpp"
#include "glm/gtc/noise.hpp"


class ChunkManager {
public:
    ChunkManager();
    ~ChunkManager();

    // Main thread asks for a chunk
    void RequestChunk(int x, int z);

    // Main thread checks if any chunks are done
    // Returns true if a chunk was retrieved
    bool GetFinishedChunk(Chunk*& outChunk);

    bool IsQueued(int x, int z);

    // This allows the thread to calculate neighbors without a World object
    static BlockState GlobalTerrainFunction(const int x, const int y, const int z) {
        int height = static_cast<int>(glm::simplex(glm::vec2(x * 0.02f, z * 0.02f)) * 10.0f + 10.0f +
           (glm::simplex(glm::vec2(x * 0.1f, z * 0.1f)) * 2.0f + 2.0f) +
           32.0f); // Base level
        height = glm::clamp(height, 1, CHUNK_HEIGHT - 1);

        if (y < height - 3)
            return BlockState::getBasic(BlockType::Stone);
        if (y < height)
            return BlockState::getBasic(BlockType::Dirt);
        if (y == height)
            return BlockState::getBasic(BlockType::Grass);
        return BlockState::getBasic(BlockType::Air);
    }

private:
    void WorkerThread();

    // 1. Job Queue (What needs to be generated)
    std::unordered_set<uint64_t> m_QueuedPositions;
    static uint64_t PackPos(const int x, const int z) noexcept {
        return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) |
               static_cast<uint32_t>(z);
    }
    std::queue<std::pair<int, int>> m_Jobs;
    std::mutex m_JobMutex;
    std::condition_variable m_JobCondition;

    // 2. Result Queue (What is ready to be uploaded)
    std::queue<Chunk*> m_Results;
    std::mutex m_ResultMutex;

    std::atomic<bool> m_Running;
    std::thread m_Thread;
};
