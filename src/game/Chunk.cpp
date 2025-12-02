#include "game/Chunk.hpp"
#include <iostream>

#include "game/World.hpp"
#include "glm/gtc/noise.hpp"
#define GLM_ENABLE_EXPERIMENTAL // to use glm::compatibility
#include <glm/gtx/compatibility.hpp>


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

float SmoothStep(const float edge0, const float edge1, const float x) {
    const float t = glm::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Use terrain copy to be thread safe
void Chunk::GenerateSimpleTerrain(const TerrainNoise noise) {
    for (int x = 0; x < CHUNK_WIDTH; ++x) {
        for (int z = 0; z < CHUNK_WIDTH; ++z) {

            // Coordinates & Noise
            const auto gx = static_cast<float>(x + cx * CHUNK_WIDTH);
            const auto gz = static_cast<float>(z + cz * CHUNK_WIDTH);
            if (gx > 100000 || gz > 100000) {
                std::cout << "Large gx/gz in chunk generation: " << gx << ", " << gz << std::endl;
            }

            // Fetch raw noise (-1.0 to 1.0)
            float contVal = noise.continental.GetNoise(gx, gz); // Range -1 to 1
            float moistVal = noise.moisture.GetNoise(gx, gz);

            // For mountains map range -1..1 to 0..1 for easier math
            float mountBaseVal = (noise.mountainBase.GetNoise(gx, gz) + 1.0f) * 0.5f;
            float peakVal = (noise.peaks.GetNoise(gx, gz) + 1.0f) * 0.5f;

            // A. Base Terrain (Plains/Hills)
            // Maps -1..1 to roughly 60..90 - Gentle rolling hills
            float plainsHeight = SEA_LEVEL + 4.0f + (contVal * 10.0f);

            // B. Mountain Terrain
            // Formula: Base Height + (MountainMass * PeakDetail)
            // 1. mountainBaseVal: Defines the big "humps" of the mountains.
            // 2. peakVal: Adds the texture on top.
            // 3. Squaring mountBaseVal (pow 2) makes the valleys between mountains wider.
            float mountainHeight = 80.0f; // Mountains start at y=80
            mountainHeight += (std::pow(mountBaseVal, 2.0f) * 100.0f); // The Mass (up to +100 height)
            mountainHeight += (peakVal * 20.0f); // The Jagged detail (up to +20 height)

            // --- The Blender ---

            // Define where mountains appear based on Continentalness
            // -1.0 to 0.3 = Ocean/Plains
            // 0.3 to 0.6 = Transition (Foothills)
            // 0.6 to 1.0 = Full Mountain Range
            float blend = SmoothStep(0.3f, 0.5f, contVal);
            float rawHeight = glm::mix(plainsHeight, mountainHeight, blend);

            // River/Valley Carving
            // If the mountain base noise is very low (a valley inside a mountain range),
            // we pull the height down slightly so it doesn't look like a solid wall.
            if (blend > 0.5f && mountBaseVal < 0.2f) {
                rawHeight -= (0.2f - mountBaseVal) * 20.0f;
            }

            int finalHeight = static_cast<int>(rawHeight);
            finalHeight = glm::clamp(finalHeight, 1, CHUNK_HEIGHT - 1);

            // --- Biome Determination ---
            BiomeType biome;
            if (finalHeight <= SEA_LEVEL) biome = BiomeType::Ocean;
            else if (finalHeight <= SEA_LEVEL + 4) biome = BiomeType::Beach;
            else if (blend > 0.5f) biome = BiomeType::Mountain;
            else biome = (moistVal > 0.0f) ? BiomeType::Forest : BiomeType::Plains;

            // Fill Blocks
            for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                BlockState blockToSet = BlockState::getBasic(BlockType::Air);

                if (y <= finalHeight) {
                    if (y < finalHeight - 4) {
                        blockToSet = BlockState::getBasic(BlockType::Stone);
                    } else {
                        // Surface Logic
                        if (biome == BiomeType::Mountain) {
                            // Snow peaks logic
                            if (y > 130.0 + (peakVal * 10))
                                blockToSet = BlockState::getBasic(BlockType::Snow);
                            else if (y > 90) // Exposed rock on mountains
                                blockToSet = BlockState::getBasic(BlockType::Stone);
                            else // Grassy foothills
                                blockToSet = BlockState::getBasic(BlockType::Grass);
                        }
                        else if (biome == BiomeType::Ocean || biome == BiomeType::Beach) {
                            blockToSet = BlockState::getBasic(BlockType::Sand);
                        }
                        else {
                            // Standard grass/dirt
                            if (y == finalHeight) blockToSet = BlockState::getBasic(BlockType::Grass);
                            else blockToSet = BlockState::getBasic(BlockType::Dirt);
                        }
                    }
                } else if (y <= SEA_LEVEL) {
                    blockToSet = BlockState::getBasic(BlockType::Water);
                }
                m_Blocks[x][y][z] = blockToSet;
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
                const float fx = static_cast<float>(x);
                const float fy = static_cast<float>(y);
                const float fz = static_cast<float>(z);

                // Add chunk offset to get world coords
                const float wx = fx + worldOffsetX;
                const float wz = fz + worldOffsetZ;

                // Top Face (y+)
                if (BlockDatabase::IsTransparent(GetBlock(x, y + 1, z))) {
                    float layer = BlockDatabase::GetTextureLayer(currentType, BlockFace::Top);
                    AddFace({wx,     fy + 1, wz},
                            {wx,     fy + 1, wz + 1},
                            {wx + 1, fy + 1, wz + 1},
                            {wx + 1, fy + 1, wz},
                            layer, uv1, uv4, uv3, uv2);
                }

                // Bottom Face (y-)
                if (BlockDatabase::IsTransparent(GetBlock(x, y - 1, z))) {
                    float layer = BlockDatabase::GetTextureLayer(currentType, BlockFace::Bottom);
                    AddFace({wx,     fy, wz},
                            {wx + 1, fy, wz},
                            {wx + 1, fy, wz + 1},
                            {wx,     fy, wz + 1},
                            layer, uv1, uv2, uv3, uv4);
                }

                // Front Face (z+)
                BlockState neighborZ_pos;
                if (z + 1 >= CHUNK_WIDTH) {
                    if (Chunk* neighbor = world.getChunk(cx, cz + 1)) {
                        neighborZ_pos = neighbor->GetBlock(x, y, 0); // local z is 0
                    } else {
                        neighborZ_pos = BlockState(BlockType::Air); // No chunk, draw face
                    }
                } else neighborZ_pos = GetBlock(x, y, z + 1); // Internal check
                if (BlockDatabase::IsTransparent(neighborZ_pos)) {
                    float layer = BlockDatabase::GetTextureLayer(currentType, BlockFace::Side);
                    AddFace({wx,     fy,     wz + 1},
                            {wx + 1, fy,     wz + 1},
                            {wx + 1, fy + 1, wz + 1},
                            {wx,     fy + 1, wz + 1},
                            layer, uv1, uv2, uv3, uv4);
                }

                // Back Face (z-)
                BlockState neighborZ_neg;
                if (z - 1 < 0) {
                    Chunk* neighbor = world.getChunk(cx, cz - 1);
                    if (neighbor) {
                        neighborZ_neg = neighbor->GetBlock(x, y, CHUNK_WIDTH - 1); // local z is max
                    } else {
                        neighborZ_neg = BlockState(BlockType::Air); // No chunk, draw face
                    }
                } else neighborZ_neg = GetBlock(x, y, z - 1); // Internal check
                if (BlockDatabase::IsTransparent(neighborZ_neg)) {
                    float layer = BlockDatabase::GetTextureLayer(currentType, BlockFace::Side);
                    AddFace({wx + 1, fy,     wz},
                            {wx,     fy,     wz},
                            {wx,     fy + 1, wz},
                            {wx + 1, fy + 1, wz},
                            layer, uv1, uv2, uv3, uv4);
                }

                // Right Face (x+)
                BlockState neighborX_pos;
                if (x + 1 >= CHUNK_WIDTH) {
                    Chunk* neighbor = world.getChunk(cx + 1, cz);
                    if (neighbor) {
                        neighborX_pos = neighbor->GetBlock(0, y, z); // local x is 0
                    } else {
                        neighborX_pos = BlockState(BlockType::Air); // No chunk, draw face
                    }
                } else neighborX_pos = GetBlock(x + 1, y, z); // Internal check
                if (BlockDatabase::IsTransparent(neighborX_pos)) {
                    float layer = BlockDatabase::GetTextureLayer(currentType, BlockFace::Side);
                    AddFace({wx + 1, fy,     wz + 1},
                            {wx + 1, fy,     wz},
                            {wx + 1, fy + 1, wz},
                            {wx + 1, fy + 1, wz + 1},
                            layer, uv1, uv2, uv3, uv4);
                }

                // Left Face (x-)
                BlockState neighborX_neg;
                if (x - 1 < 0) {
                    Chunk* neighbor = world.getChunk(cx - 1, cz);
                    if (neighbor) {
                        neighborX_neg = neighbor->GetBlock(CHUNK_WIDTH - 1, y, z); // local x is max
                    } else {
                        neighborX_neg = BlockState(BlockType::Air); // No chunk, draw face
                    }
                } else neighborX_neg = GetBlock(x - 1, y, z); // Internal check
                if (BlockDatabase::IsTransparent(neighborX_neg)) {
                    float layer = BlockDatabase::GetTextureLayer(currentType, BlockFace::Side);
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
