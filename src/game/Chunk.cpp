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

// Helper: Remaps a value from one range to another
float Remap(float value, float min1, float max1, float min2, float max2) {
    return min2 + (value - min1) * (max2 - min2) / (max1 - min1);
}

// Helper: Creates "steps" in the terrain (The Minecraft look)
float Terrace(float value, float stepSize) {
    return std::round(value / stepSize) * stepSize;
}

// Use terrain copy to be thread safe
float GetHeightFromCurve(float continentalness, float peaks) {
    // continentalness is -1.0 to 1.0.

    // 1. DEEP OCEAN (-1.0 to -0.6)
    if (continentalness < -0.6f) {
        return Remap(continentalness, -1.0f, -0.6f, 10.0f, 40.0f);
    }
    // 2. OCEAN / BEACH (-0.6 to -0.15)
    if (continentalness < -0.15f) {
        return Remap(continentalness, -0.6f, -0.15f, 40.0f, 62.0f);
    }
    // 3. GREAT PLAINS / VALLEY FLOOR (-0.15 to 0.3)
    // *Key Feature*: A wide range of noise maps to a very small height range.
    // This creates the flat valley floor seen in your image.
    if (continentalness < 0.3f) {
        return Remap(continentalness, -0.15f, 0.3f, 65.0f, 75.0f);
    }
    // 4. CLIFFS / HIGH PLATEAU (0.3 to 1.0)
    // Sudden jump in height

    // Base height for mountains
    float base = Remap(continentalness, 0.3f, 1.0f, 100.0f, 180.0f);

    // Add jagged peaks ON TOP of the plateau
    // We only add peaks if we are in the mountain zone
    return base + (peaks * 30.0f);
}

void Chunk::GenerateSimpleTerrain(const TerrainNoise& noise) {
    // Prepare a list of trees to generate after the terrain is filled
    std::vector<Tree> treesToGen;

    for (int x = 0; x < CHUNK_WIDTH; ++x) {
        for (int z = 0; z < CHUNK_WIDTH; ++z) {

            float gx = static_cast<float>(x + cx * CHUNK_WIDTH);
            float gz = static_cast<float>(z + cz * CHUNK_WIDTH);

            // --- A. Domain Warping ---
            // This makes the terrain look like it "flows" rather than just being round blobs.
            // We distort the coordinates slightly before sampling the main noise.
            float warpX = noise.moisture.GetNoise(gx, gz) * 40.0f; // Uses moisture noise for warp
            float warpZ = noise.peaks.GetNoise(gx, gz) * 40.0f;

            // --- B. Sample Noise ---
            // Note: We use very low frequency for continental to get MASSIVE biomes
            float contVal = noise.continental.GetNoise(gx + warpX, gz + warpZ);
            float peakVal = noise.peaks.GetNoise(gx, gz); // High frequency detail

            // --- C. Calculate Height ---
            // Use the Spline approach
            float rawHeight = GetHeightFromCurve(contVal, std::abs(peakVal));

            // Apply Terracing (Stepped look from the image)
            // Steps of 2 blocks for a stylized look, or 1 for smooth
            rawHeight = Terrace(rawHeight, 1.0f);

            int height = static_cast<int>(rawHeight);
            height = glm::clamp(height, 1, CHUNK_HEIGHT - 1);

            // --- D. Biome Determination ---
            // We use the height directly to determine the surface block,
            // which creates consistent sedimentary layers.
            BlockType surfaceBlock = BlockType::Grass;
            if (height < 64) surfaceBlock = BlockType::Sand; // Underwater/Beach
            else if (height > 160) surfaceBlock = BlockType::Snow;
            else if (height > 120 && peakVal > 0.5f) surfaceBlock = BlockType::Stone; // Cliffs

            // --- E. Fill Blocks ---
            for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                if (y > height) {
                    if (y <= 62) m_Blocks[x][y][z] = BlockState::getBasic(BlockType::Water);
                    else m_Blocks[x][y][z] = BlockState::getBasic(BlockType::Air);
                }
                else if (y == height) {
                    m_Blocks[x][y][z] = BlockState::getBasic(surfaceBlock);

                    // --- F. Tree Planning ---
                    // Only plant trees on Grass, above water, and NOT too high up
                    // Use a noise value for density so they clump in forests
                    if (surfaceBlock == BlockType::Grass && y > 64 && y < 140) {
                        // "Tree Density" noise
                        float density = noise.moisture.GetNoise(gx * 2.0f, gz * 2.0f);
                        if (density > 0.3f) { // Forest areas
                            // Random chance per block (pseudo-random based on coord)
                            if (WorldGen::ChancePercentFromCoords(gx, gz, 2)) { // 2% chance per block in forest
                                treesToGen.push_back({x, y + 1, z, 5, (int)BlockType::Wood, (int)BlockType::Leaves});
                            }
                        }
                    }
                }
                else if (y > height - 4) {
                    m_Blocks[x][y][z] = BlockState::getBasic(BlockType::Dirt);
                }
                else {
                    m_Blocks[x][y][z] = BlockState::getBasic(BlockType::Stone);
                }
            }
        }
    }

    // --- G. Generate Trees ---
    // Note: In a real engine, this is done in a separate "Population" pass
    // to handle trees overlapping chunk borders. For now, we clamp to chunk.
    for (const auto& tree : treesToGen) {
        GenerateTree(tree);
    }
}

// Proper Tree Generation
void Chunk::GenerateTree(const Tree& tree) {
    // Trunk
    for (int i = 0; i < tree.height; ++i) {
        SetBlock(tree.x, tree.y + i, tree.z, BlockState::getBasic((BlockType)tree.trunkType));
    }

    // Leaves (Simple Sphere/Blob)
    int leafStart = tree.y + tree.height - 2;
    int leafEnd = tree.y + tree.height + 1;
    int radius = 2;

    for (int ly = leafStart; ly <= leafEnd; ++ly) {
        for (int lx = tree.x - radius; lx <= tree.x + radius; ++lx) {
            for (int lz = tree.z - radius; lz <= tree.z + radius; ++lz) {

                // Distance check for "round" look
                int dX = lx - tree.x;
                int dY = ly - (tree.y + tree.height - 1); // Center roughly at top of trunk
                int dZ = lz - tree.z;

                // A simple distance squared check (sphere-ish)
                if (dX*dX + dY*dY + dZ*dZ <= radius*radius + 1) {
                    // Don't overwrite the trunk
                    if (GetBlock(lx, ly, lz).type == BlockType::Air) {
                         SetBlock(lx, ly, lz, BlockState::getBasic(static_cast<BlockType>(tree.leafType)));
                    }
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
                if (currentType == BlockType::Air) {
                    continue;
                }

                // Local voxel coords
                const float fx = static_cast<float>(x);
                const float fy = static_cast<float>(y);
                const float fz = static_cast<float>(z);

                // Add chunk offset to get world coords
                const float wx = fx + worldOffsetX;
                const float wz = fz + worldOffsetZ;

                // Helper Logic for Self-Culling
                auto ShouldDrawFace = [&](BlockState neighbor) -> bool {
                    // If neighbor is opaque (Stone), we never see this face.
                    if (!BlockDatabase::IsTransparent(neighbor)) return false;

                    // If neighbor is the exact same type (Water vs Water), cull the face.
                    if (neighbor.type == currentType && currentType != BlockType::Air) return false;

                    return true;
                };

                // Top Face (y+)
                if (ShouldDrawFace(GetBlock(x, y + 1, z))) {
                    float layer = BlockDatabase::GetTextureLayer(currentType, BlockFace::Top);
                    AddFace({wx,     fy + 1, wz},
                            {wx,     fy + 1, wz + 1},
                            {wx + 1, fy + 1, wz + 1},
                            {wx + 1, fy + 1, wz},
                            layer, uv1, uv4, uv3, uv2);
                }

                // Bottom Face (y-)
                if (ShouldDrawFace(GetBlock(x, y - 1, z))) {
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
                if (ShouldDrawFace(neighborZ_pos)) {
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
                if (ShouldDrawFace(neighborZ_neg)) {
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
                if (ShouldDrawFace(neighborX_pos)) {
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
                if (ShouldDrawFace(neighborX_neg)) {
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
