#pragma once

#include <vector>

#include "Block.hpp"
#include "glm/glm.hpp"

// Define chunk dimensions
constexpr int CHUNK_WIDTH = 16; // x
constexpr int CHUNK_HEIGHT = 128; // y
constexpr int CHUNK_DEPTH = 16; // z

class Chunk {
public:
    Chunk();
    ~Chunk();

    // Only for debugging, until we have a proper gen
    void GenerateSimpleTerrain();

    // Core voxel logic
    void BuildMesh();

    // Helper to get a block, with bounds checking
    BlockState GetBlock(int x, int y, int z) const;
    void SetBlock(int x, int y, int z, BlockState type);

    // Data access for the renderer
    const std::vector<float>& GetMeshVertices() const { return m_MeshVertices; }
    int GetVertexCount() const { return m_VertexCount; }

private:
    BlockState m_Blocks[CHUNK_WIDTH][CHUNK_HEIGHT][CHUNK_DEPTH];

    std::vector<float> m_MeshVertices; // x, y, z, u, v
    int m_VertexCount;

    // Helper to add a face to the mesh
    void AddFace(
        const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, const glm::vec3& p4,
        float textureLayer, // Pass the layer index directly
        const glm::vec2& uv1, const glm::vec2& uv2, const glm::vec2& uv3, const glm::vec2& uv4
    );
};
