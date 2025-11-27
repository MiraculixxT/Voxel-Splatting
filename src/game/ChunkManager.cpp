#include "game/ChunkManager.hpp"
#include "game/Chunk.hpp"
#include "glm/gtc/noise.hpp"

ChunkManager::ChunkManager() : m_Running(true) {
    m_Thread = std::thread(&ChunkManager::WorkerThread, this);
}

ChunkManager::~ChunkManager() {
    m_Running = false;
    m_JobCondition.notify_all();
    if (m_Thread.joinable()) m_Thread.join();
}

void ChunkManager::RequestChunk(int x, int z) {
    uint64_t key = PackPos(x, z);
    bool inserted = false;
    {
        std::lock_guard lock(m_JobMutex);
        if (m_QueuedPositions.emplace(key).second) {
            m_Jobs.emplace(x, z);
            inserted = true;
        }
    }
    if (inserted) m_JobCondition.notify_one();
}

bool ChunkManager::GetFinishedChunk(Chunk*& outChunk) {
    std::lock_guard lock(m_ResultMutex);
    if (m_Results.empty()) return false;

    outChunk = m_Results.front();
    m_Results.pop();
    return true;
}

bool ChunkManager::IsQueued(const int x, const int z) {
    std::lock_guard lock(m_JobMutex);
    return m_QueuedPositions.contains(PackPos(x, z));
}

void ChunkManager::WorkerThread() {
    while (m_Running) {
        std::pair<int, int> job;

        // Wait for job
        {
            std::unique_lock lock(m_JobMutex);
            m_JobCondition.wait(lock, [this]{ return !m_Jobs.empty() || !m_Running; });
            if (!m_Running) return;
            job = m_Jobs.front();
            m_Jobs.pop();
            m_QueuedPositions.erase(PackPos(job.first, job.second));
        }

        // --- HEAVY WORK (Off Main Thread) ---

        // Allocate
        auto* newChunk = new Chunk(job.first, job.second);

        // Generate Blocks
        newChunk->GenerateSimpleTerrain();

        // Build Mesh (pass terrain function again for async neighbour checks)
        // TODO: This must be revisited on block modifications
        newChunk->BuildMesh([](const int x, const int y, const int z) {
            return GlobalTerrainFunction(x, y, z);
        });

        // Push to Results
        {
            std::lock_guard lock(m_ResultMutex);
            m_Results.push(newChunk);
        }
    }
}
