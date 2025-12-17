#pragma once

#include <vector>
#include <glm/glm.hpp>
#include "Block.hpp"
#include "WorldGen.hpp"

// Define chunk dimensions.
constexpr int CHUNK_WIDTH = 16;
constexpr int CHUNK_HEIGHT = 256;

// Forward declare World to avoid a circular dependency
class World;
struct TerrainNoise;

/**
 * @class Chunk
 * @brief Represents a vertical column of blocks in the world.
 */
class Chunk {
public:
    // Pointer back to the owning world, used for cross-chunk structure placement.
    World* world = nullptr;

    /**
     * @brief The chunk's X and Z coordinates in the world grid.
     * These are public and const as they define the chunk's identity.
     */
    int cx, cz;

    /**
     * @param cx The chunk's X coordinate.
     * @param cz The chunk's Z coordinate.
     */
    Chunk(int cx, int cz);
    ~Chunk();

    /**
     * @brief Fills the chunk with simple procedural terrain.
     */
    void GenerateSimpleTerrain(const TerrainNoise& noise);

    void GenerateTree(const Tree& tree);

    /**
     * @brief Gets the block state at local chunk coordinates (0-15, 0-255, 0-15).
     * If coordinates are out of these bounds, returns BlockType::Air.
     */
    BlockState GetBlock(int x, int y, int z) const;

    /**
     * @brief Sets the block at local chunk coordinates.
     * Does nothing if coordinates are out of bounds.
     */
    void SetBlock(int x, int y, int z, BlockState type);

    /**
     * @brief Builds the chunk's render mesh.
     * This is the core logic for visible face culling, including
     * checking neighbor chunks via the world.
     * @param world The world object, used to query for neighbor blocks.
     */
    void BuildMesh(World& world);

    /**
     * @brief Gets the generated mesh vertices after BuildMesh() is called.
     * Vertex format is (x, y, z, u, v, layer) - 6 floats.
     */
    const std::vector<float>& GetMeshVertices() const { return m_MeshVertices; }

    /**
     * @brief Gets the number of vertices in the current mesh.
     */
    int GetVertexCount() const { return m_VertexCount; }

private:
    /**
     * @brief Helper to add a 6-vertex quad (2 triangles) to the mesh data.
     */
    void AddFace(const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, const glm::vec3& p4,
                 float textureLayer,
                 const glm::vec2& uv1, const glm::vec2& uv2, const glm::vec2& uv3, const glm::vec2& uv4);

    // 3D array of blocks [x][y][z]
    BlockState m_Blocks[CHUNK_WIDTH][CHUNK_HEIGHT][CHUNK_WIDTH];

    // Render data
    std::vector<float> m_MeshVertices;
    int m_VertexCount;
};