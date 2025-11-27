#include "game/Chunk.hpp"
#include <iostream>

#include "game/World.hpp"
#include "glm/gtc/noise.hpp"

Chunk::Chunk(const int cx, const int cz) : cx(cx), cz(cz), m_Blocks{}, m_VertexCount(0) {
    // Initialize all blocks to Air
    for (int x = 0; x < CHUNK_WIDTH; ++x) {
        for (int y = 0; y < CHUNK_HEIGHT; ++y) {
            for (int z = 0; z < CHUNK_WIDTH; ++z) {
                m_Blocks[x][y][z] = BlockState();
            }
        }
    }
}

Chunk::~Chunk() {
    // No more GL objects to clean up here
}

void Chunk::GenerateSimpleTerrain() {
    for (int x = 0; x < CHUNK_WIDTH; ++x) {
        for (int z = 0; z < CHUNK_WIDTH; ++z) {
            const int dx = x + cx * CHUNK_WIDTH;
            const int dz = z + cz * CHUNK_WIDTH;

            bool foundSurface = false;
            for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                if (foundSurface) {
                    m_Blocks[x][y][z] = BlockState(BlockType::Air);
                    continue;
                }
                const auto block = ChunkManager::GlobalTerrainFunction(dx, y, dz);
                m_Blocks[x][y][z] = block;
                if (block.type == BlockType::Air) {
                    foundSurface = true;
                }
            }
        }
    }
}

BlockState Chunk::GetBlock(int x, int y, int z) const {
    // Check bounds. If out of bounds, return Air (so faces are drawn at chunk borders)
    if (x < 0 || x >= CHUNK_WIDTH ||
        y < 0 || y >= CHUNK_HEIGHT ||
        z < 0 || z >= CHUNK_WIDTH) {
        return BlockState(BlockType::Air);
    }
    return m_Blocks[x][y][z];
}

void Chunk::SetBlock(int x, int y, int z, BlockState type) {
    if (x < 0 || x >= CHUNK_WIDTH ||
        y < 0 || y >= CHUNK_HEIGHT ||
        z < 0 || z >= CHUNK_WIDTH) {
        return; // Out of bounds
    }
    m_Blocks[x][y][z] = type;
}

// Helper to add vertex data for one face
void Chunk::AddFace(const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, const glm::vec3& p4,
                    float textureLayer, // This is now a layer index
                    const glm::vec2& uv1, const glm::vec2& uv2, const glm::vec2& uv3, const glm::vec2& uv4) {

    // The vertex format is (x, y, z, u, v, layer) - 6 floats

    // Tri 1
    m_MeshVertices.push_back(p1.x); m_MeshVertices.push_back(p1.y); m_MeshVertices.push_back(p1.z);
    m_MeshVertices.push_back(uv1.x); m_MeshVertices.push_back(uv1.y); m_MeshVertices.push_back(textureLayer);

    m_MeshVertices.push_back(p2.x); m_MeshVertices.push_back(p2.y); m_MeshVertices.push_back(p2.z);
    m_MeshVertices.push_back(uv2.x); m_MeshVertices.push_back(uv2.y); m_MeshVertices.push_back(textureLayer);

    m_MeshVertices.push_back(p3.x); m_MeshVertices.push_back(p3.y); m_MeshVertices.push_back(p3.z);
    m_MeshVertices.push_back(uv3.x); m_MeshVertices.push_back(uv3.y); m_MeshVertices.push_back(textureLayer);

    // Tri 2
    m_MeshVertices.push_back(p1.x); m_MeshVertices.push_back(p1.y); m_MeshVertices.push_back(p1.z);
    m_MeshVertices.push_back(uv1.x); m_MeshVertices.push_back(uv1.y); m_MeshVertices.push_back(textureLayer);

    m_MeshVertices.push_back(p3.x); m_MeshVertices.push_back(p3.y); m_MeshVertices.push_back(p3.z);
    m_MeshVertices.push_back(uv3.x); m_MeshVertices.push_back(uv3.y); m_MeshVertices.push_back(textureLayer);

    m_MeshVertices.push_back(p4.x); m_MeshVertices.push_back(p4.y); m_MeshVertices.push_back(p4.z);
    m_MeshVertices.push_back(uv4.x); m_MeshVertices.push_back(uv4.y); m_MeshVertices.push_back(textureLayer);
}

void Chunk::BuildMesh(World &world) {
    BuildMesh([&world](const int x, const int y, const int z) {
        return world.getBlock(x, y, z);
    });
}

void Chunk::BuildMesh(const std::function<BlockState(int, int, int)>& blockGetter) {
    m_MeshVertices.clear();
    m_VertexCount = 0;

    // Standard UV coords for a full quad
    const glm::vec2 uv1(0.0f, 0.0f);
    const glm::vec2 uv2(1.0f, 0.0f);
    const glm::vec2 uv3(1.0f, 1.0f);
    const glm::vec2 uv4(0.0f, 1.0f);

    // World offset of this chunk
    const auto worldOffsetX = static_cast<float>(cx * CHUNK_WIDTH);
    const auto worldOffsetZ = static_cast<float>(cz * CHUNK_WIDTH);

    for (int x = 0; x < CHUNK_WIDTH; ++x) {
        for (int y = 0; y < CHUNK_HEIGHT; ++y) {
            for (int z = 0; z < CHUNK_WIDTH; ++z) {
                BlockState currentBlock = m_Blocks[x][y][z];
                BlockType currentType = currentBlock.type;
                if (BlockDatabase::IsTransparent(currentType)) {
                    continue;
                }

                // Local voxel coords
                const auto fx = static_cast<float>(x);
                const auto fy = static_cast<float>(y);
                const auto fz = static_cast<float>(z);

                // Add chunk offset to get world coords
                const float wx = fx + worldOffsetX;
                const float wz = fz + worldOffsetZ;

                // Top Face (y+)
                if (BlockDatabase::IsTransparent(GetBlock(x, y + 1, z))) {
                    const float layer = BlockDatabase::GetTextureLayer(currentType, BlockFace::Top);
                    AddFace({wx,     fy + 1, wz},
                            {wx,     fy + 1, wz + 1},
                            {wx + 1, fy + 1, wz + 1},
                            {wx + 1, fy + 1, wz},
                            layer, uv1, uv4, uv3, uv2);
                }

                // Bottom Face (y-)
                if (BlockDatabase::IsTransparent(GetBlock(x, y - 1, z))) {
                    const float layer = BlockDatabase::GetTextureLayer(currentType, BlockFace::Bottom);
                    AddFace({wx,     fy, wz},
                            {wx + 1, fy, wz},
                            {wx + 1, fy, wz + 1},
                            {wx,     fy, wz + 1},
                            layer, uv1, uv2, uv3, uv4);
                }

                // Front Face (z+)
                const BlockState neighborZ_pos = (z + 1 < CHUNK_WIDTH)
                                 ? m_Blocks[x][y][z+1]
                                 : blockGetter(this->cx * CHUNK_WIDTH + x, y, this->cz * CHUNK_WIDTH + z + 1);
                if (BlockDatabase::IsTransparent(neighborZ_pos)) {
                    const float layer = BlockDatabase::GetTextureLayer(currentType, BlockFace::Side);
                    AddFace({wx,     fy,     wz + 1},
                            {wx + 1, fy,     wz + 1},
                            {wx + 1, fy + 1, wz + 1},
                            {wx,     fy + 1, wz + 1},
                            layer, uv1, uv2, uv3, uv4);
                }

                // Back Face (z-)
                const BlockState neighborZ_neg = (z - 1 < CHUNK_WIDTH)
                                 ? m_Blocks[x][y][z-1]
                                 : blockGetter(this->cx * CHUNK_WIDTH + x, y, this->cz * CHUNK_WIDTH + z - 1);
                if (BlockDatabase::IsTransparent(neighborZ_neg)) {
                    const float layer = BlockDatabase::GetTextureLayer(currentType, BlockFace::Side);
                    AddFace({wx + 1, fy,     wz},
                            {wx,     fy,     wz},
                            {wx,     fy + 1, wz},
                            {wx + 1, fy + 1, wz},
                            layer, uv1, uv2, uv3, uv4);
                }

                // Right Face (x+)
                const BlockState neighborX_pos = (x + 1 < CHUNK_WIDTH)
                                 ? m_Blocks[x+1][y][z]
                                 : blockGetter(this->cx * CHUNK_WIDTH + x + 1, y, this->cz * CHUNK_WIDTH + z);
                if (BlockDatabase::IsTransparent(neighborX_pos)) {
                    const float layer = BlockDatabase::GetTextureLayer(currentType, BlockFace::Side);
                    AddFace({wx + 1, fy,     wz + 1},
                            {wx + 1, fy,     wz},
                            {wx + 1, fy + 1, wz},
                            {wx + 1, fy + 1, wz + 1},
                            layer, uv1, uv2, uv3, uv4);
                }

                // Left Face (x-)
                const BlockState neighborX_neg = (x - 1 < CHUNK_WIDTH)
                                 ? m_Blocks[x-1][y][z]
                                 : blockGetter(this->cx * CHUNK_WIDTH + x - 1, y, this->cz * CHUNK_WIDTH + z);
                if (BlockDatabase::IsTransparent(neighborX_neg)) {
                    const float layer = BlockDatabase::GetTextureLayer(currentType, BlockFace::Side);
                    AddFace({wx, fy,     wz},
                            {wx, fy,     wz + 1},
                            {wx, fy + 1, wz + 1},
                            {wx, fy + 1, wz},
                            layer, uv1, uv2, uv3, uv4);
                }
            }
        }
    }

    m_VertexCount = m_MeshVertices.size() / 6;
}
