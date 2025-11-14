#include "game/Chunk.hpp"
#include <iostream>

// Simple noise function for terrain debugging
float simpleNoise(int x, int z) {
    return (sin(x * 0.1f) + sin(z * 0.1f)) * 4.0f + 8.0f; // Basic sine wave "hills"
}

Chunk::Chunk() : m_Blocks{}, m_VertexCount(0) {
    // Initialize all blocks to Air
    for (int x = 0; x < CHUNK_WIDTH; ++x) {
        for (int y = 0; y < CHUNK_HEIGHT; ++y) {
            for (int z = 0; z < CHUNK_DEPTH; ++z) {
                m_Blocks[x][y][z] = BlockHelper::getAir();
            }
        }
    }
}

Chunk::~Chunk() {
    // No more GL objects to clean up here
}

void Chunk::GenerateSimpleTerrain() {
    for (int x = 0; x < CHUNK_WIDTH; ++x) {
        for (int z = 0; z < CHUNK_DEPTH; ++z) {
            int height = static_cast<int>(simpleNoise(x, z));
            height = glm::clamp(height, 1, CHUNK_HEIGHT - 1);

            for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                if (y < height - 3)
                    m_Blocks[x][y][z] = BlockHelper::getBasic(BlockType::Stone);
                else if (y < height)
                    m_Blocks[x][y][z] = BlockHelper::getBasic(BlockType::Dirt);
                else if (y == height)
                    m_Blocks[x][y][z] = BlockHelper::getBasic(BlockType::Grass);
                else
                    m_Blocks[x][y][z] = BlockHelper::getBasic(BlockType::Air);
            }
        }
    }
}

BlockState Chunk::GetBlock(int x, int y, int z) const {
    // Check bounds. If out of bounds, return Air (so faces are drawn at chunk borders)
    if (x < 0 || x >= CHUNK_WIDTH ||
        y < 0 || y >= CHUNK_HEIGHT ||
        z < 0 || z >= CHUNK_DEPTH) {
        return BlockHelper::getAir();
    }
    return m_Blocks[x][y][z];
}

void Chunk::SetBlock(int x, int y, int z, BlockState type) {
    if (x < 0 || x >= CHUNK_WIDTH ||
        y < 0 || y >= CHUNK_HEIGHT ||
        z < 0 || z >= CHUNK_DEPTH) {
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


void Chunk::BuildMesh() {
    m_MeshVertices.clear();
    m_VertexCount = 0;

    // Standard UV coords for a full quad
    const glm::vec2 uv1(0.0f, 0.0f);
    const glm::vec2 uv2(1.0f, 0.0f);
    const glm::vec2 uv3(1.0f, 1.0f);
    const glm::vec2 uv4(0.0f, 1.0f);

    for (int x = 0; x < CHUNK_WIDTH; ++x) {
        for (int y = 0; y < CHUNK_HEIGHT; ++y) {
            for (int z = 0; z < CHUNK_DEPTH; ++z) {
                BlockState currentBlock = m_Blocks[x][y][z];
                BlockType currentType = currentBlock.type;
                if (BlockDatabase::IsTransparent(currentType)) {
                    continue; // Skip invis blocks
                }

                // Voxel coordinates (center of the block)
                auto fx = static_cast<float>(x);
                auto fy = static_cast<float>(y);
                auto fz = static_cast<float>(z);

                // Check 6 neighbors. If neighbor is invis, add a face.

                // Top Face (y+)
                if (BlockDatabase::IsTransparent(GetBlock(x, y + 1, z))) {
                    float layer = BlockDatabase::GetTextureLayer(currentType, BlockFace::Top);
                    AddFace({fx, fy + 1, fz}, {fx, fy + 1, fz + 1}, {fx + 1, fy + 1, fz + 1}, {fx + 1, fy + 1, fz},
                            layer, uv1, uv4, uv3, uv2); // CCW winding
                }

                // Bottom Face (y-)
                if (BlockDatabase::IsTransparent(GetBlock(x, y - 1, z))) {
                    float layer = BlockDatabase::GetTextureLayer(currentType, BlockFace::Bottom);
                    AddFace({fx, fy, fz}, {fx + 1, fy, fz}, {fx + 1, fy, fz + 1}, {fx, fy, fz + 1},
                            layer, uv1, uv2, uv3, uv4);
                }

                // Front Face (z+)
                if (BlockDatabase::IsTransparent(GetBlock(x, y, z + 1))) {
                    float layer = BlockDatabase::GetTextureLayer(currentType, BlockFace::Side);
                    AddFace({fx, fy, fz + 1}, {fx + 1, fy, fz + 1}, {fx + 1, fy + 1, fz + 1}, {fx, fy + 1, fz + 1},
                            layer, uv1, uv2, uv3, uv4);
                }

                // Back Face (z-)
                if (BlockDatabase::IsTransparent(GetBlock(x, y, z - 1))) {
                    float layer = BlockDatabase::GetTextureLayer(currentType, BlockFace::Side);
                    AddFace({fx + 1, fy, fz}, {fx, fy, fz}, {fx, fy + 1, fz}, {fx + 1, fy + 1, fz},
                            layer, uv1, uv2, uv3, uv4);
                }

                // Right Face (x+)
                if (BlockDatabase::IsTransparent(GetBlock(x + 1, y, z))) {
                    float layer = BlockDatabase::GetTextureLayer(currentType, BlockFace::Side);
                    AddFace({fx + 1, fy, fz + 1}, {fx + 1, fy, fz}, {fx + 1, fy + 1, fz}, {fx + 1, fy + 1, fz + 1},
                            layer, uv1, uv2, uv3, uv4);
                }

                // Left Face (x-)
                if (BlockDatabase::IsTransparent(GetBlock(x - 1, y, z))) {
                    float layer = BlockDatabase::GetTextureLayer(currentType, BlockFace::Side);
                    AddFace({fx, fy, fz}, {fx, fy, fz + 1}, {fx, fy + 1, fz + 1}, {fx, fy + 1, fz},
                            layer, uv1, uv2, uv3, uv4);
                }
            }
        }
    }
    m_VertexCount = m_MeshVertices.size() / 5; // 5 floats per vertex (x,y,z,u,v)
}
