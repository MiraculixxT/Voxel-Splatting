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

            // 1. Warping (Keep this low for the target image look, we want predictable cliffs)
            float warp = noise.moisture.GetNoise(gx, gz) * 20.0f;

            // 2. Base Noise
            // We use 'continental' to look up our height on the Spline
            float contVal = noise.continental.GetNoise(gx + warp, gz + warp);

            // 3. Detail Noise
            // We only add "jaggedness" if we are high up. Valleys should be smooth.
            float peakVal = noise.peaks.GetNoise(gx, gz);

            // 4. Calculate Base Height via Spline
            float baseHeight = WorldGen::GetSplineHeight(contVal);

            // 5. Add Detail
            // If we are in the "Cliff" zone (contVal > 0.35), add the jagged noise
            // If we are in the valley (contVal < 0.3), keep it flat.
            float finalHeightF = baseHeight;
            if (contVal > 0.35f) {
                finalHeightF += peakVal * 15.0f;
            }

            // 6. TERRACING (The "Minecraft" Look)
            // This snaps the height to integers, creating flat spots for trees to stand on
            // The target image implies a step size of 1 or 2.
            finalHeightF = std::round(finalHeightF);

            int height = static_cast<int>(finalHeightF);
            height = glm::clamp(height, 1, CHUNK_HEIGHT - 1);

            // 7. Calculate "Slope" roughly
            // We compare this block's height to the "ideal" spline height.
            // If the Spline is rising fast, we are on a cliff wall.
            // (Simple heuristic: High continentalness + High Height = likely cliff)
            bool isCliff = (contVal > 0.35f && peakVal > 0.0f) || (height > 130);

            // --- FILL BLOCKS ---
            for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                if (y > height) {
                    if (y <= noise.SEA_LEVEL) m_Blocks[x][y][z] = BlockState::getBasic(BlockType::Water);
                    else m_Blocks[x][y][z] = BlockState::getBasic(BlockType::Air);
                }
                else if (y == height) {
                    // Top Layer Logic
                    if (height < noise.SEA_LEVEL + 2) {
                        m_Blocks[x][y][z] = BlockState::getBasic(BlockType::Sand);
                    }
                    else if (isCliff) {
                        // If it's a steep cliff, put stone on top (or dirt), not grass
                        // To get the "Green Valley" look, use Grass mostly, but Stone on very high peaks
                        if (height > 150) m_Blocks[x][y][z] = BlockState::getBasic(BlockType::Stone);
                        else m_Blocks[x][y][z] = BlockState::getBasic(BlockType::Grass);
                    }
                    else {
                        m_Blocks[x][y][z] = BlockState::getBasic(BlockType::Grass);

                        // TREE PLANTING
                        // Only plant in the "Valley" or "Foothills" (contVal < 0.6)
                        // Don't plant on super steep cliffs
                        if (contVal > -0.1f && contVal < 0.55f) {
                            // Use noise for density again
                            float density = noise.moisture.GetNoise(gx * 0.5f, gz * 0.5f); // low freq density
                            if (density > 0.0f) { // 50% of the world is forest
                                // Pseudo-random chance
                                const auto rnd = WorldGen::ChancePercentFromCoords(gx, gz);
                                if (rnd < 3) { // 3% chance per block
                                    int treeH = 5 + (rnd % 4); // 5 to 8 blocks tall
                                    treesToGen.push_back({x, y + 1, z, treeH, (int)BlockType::Wood, (int)BlockType::Leaves});
                                }
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

    // --- Generate Trees ---
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

    // Leaves: A flattened spheroid looks more natural than a perfect sphere
    int radius = 2; // Make radius wider for taller trees if desired
    if (tree.height > 6) radius = 3;

    int leafBottom = tree.y + tree.height - 3;
    int leafTop = tree.y + tree.height + 1;

    for (int ly = leafBottom; ly <= leafTop; ++ly) {
        for (int lx = tree.x - radius; lx <= tree.x + radius; ++lx) {
            for (int lz = tree.z - radius; lz <= tree.z + radius; ++lz) {

                int dx = lx - tree.x;
                int dy = ly - (tree.y + tree.height - 1);
                int dz = lz - tree.z;

                // Squared distance check with a slight "squash" factor on Y
                // This makes the tree look wider and less like a lollipop
                if ((dx*dx) + (dy*dy*2) + (dz*dz) <= (radius*radius)) {
                    // Randomly skip some blocks to make leaves look "fluffy" and transparent
                    // The second image has "noisy" leaves
                    if (((lx + ly + lz) % 7) != 0) {
                        if (GetBlock(lx, ly, lz).type == BlockType::Air) {
                            SetBlock(lx, ly, lz, BlockState::getBasic((BlockType)tree.leafType));
                        }
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


void Chunk::BuildSplats(World& world) {
    m_Splats.clear();

    const float worldOffsetX = static_cast<float>(cx * CHUNK_WIDTH);
    const float worldOffsetZ = static_cast<float>(cz * CHUNK_WIDTH);

    for (int x = 0; x < CHUNK_WIDTH; ++x) {
        for (int y = 0; y < CHUNK_HEIGHT; ++y) {
            for (int z = 0; z < CHUNK_WIDTH; ++z) {
                BlockState currentBlock = m_Blocks[x][y][z];
                BlockType currentType   = currentBlock.type;
                if (currentType == BlockType::Air) continue;

                // PERF: Water should only render its surface (avoid thousands of underwater splats)
                if (currentType == BlockType::Water) {
                    // Only emit the top face if the block above is NOT water (i.e. this is the surface)
                    BlockState above = GetBlock(x, y + 1, z);
                    if (BlockDatabase::IsTransparent(above) && above.type != BlockType::Water) {
                        // We still need center, so compute it here and emit directly
                        glm::vec3 waterCenter(
                                static_cast<float>(x) + worldOffsetX + 0.5f,
                                static_cast<float>(y) + 0.5f,
                                static_cast<float>(z) + worldOffsetZ + 0.5f
                        );

                        // Local lambda copies the needed parts from emitFaceSplats but uses the existing one below
                        // by temporarily assigning center.
                        center = waterCenter;
                        // Emit only the top surface
                        // (emitFaceSplats is declared below; we jump to it by not continuing here)
                    } else {
                        continue; // underwater water block -> no splats at all
                    }
                }

                // Block center in world space
                glm::vec3 center;
                center = glm::vec3(
                        static_cast<float>(x) + worldOffsetX + 0.5f,
                        static_cast<float>(y) + 0.5f,
                        static_cast<float>(z) + worldOffsetZ + 0.5f
                );

                auto neighborIsTransparent = [&](const BlockState& n) {
                    return BlockDatabase::IsTransparent(n);
                };

                // Helper: generates a 16x16 grid of splats on a face with a given normal
                auto emitFaceSplats = [&](const glm::vec3& faceNormal, BlockFace blockFace) {
                    // 0.5 from the center to the block surface, plus a small epsilon
                    const float surfaceOffset = 0.51f;

                    glm::vec3 n = glm::normalize(faceNormal);
                    glm::vec3 tangent;
                    glm::vec3 bitangent;

                    // Tangent/Bitangent so wählen, dass sie zur Mesh-UV-Ausrichtung passen
                    if (n.y > 0.5f) {
                        // Top (y+): u = +x, v = +z
                        tangent   = glm::vec3(1.0f, 0.0f, 0.0f);
                        bitangent = glm::vec3(0.0f, 0.0f, 1.0f);
                    } else if (n.y < -0.5f) {
                        // Bottom (y-): gleiche Ausrichtung wie Top
                        tangent   = glm::vec3(1.0f, 0.0f, 0.0f);
                        bitangent = glm::vec3(0.0f, 0.0f, 1.0f);
                    } else if (n.z > 0.5f) {
                        // Front (z+): u = +x, v = +y
                        tangent   = glm::vec3(1.0f, 0.0f, 0.0f);
                        bitangent = glm::vec3(0.0f, 1.0f, 0.0f);
                    } else if (n.z < -0.5f) {
                        // Back (z-): u = -x, v = +y (entspricht Mesh-Flip)
                        tangent   = glm::vec3(-1.0f, 0.0f, 0.0f);
                        bitangent = glm::vec3(0.0f, 1.0f, 0.0f);
                    } else if (n.x > 0.5f) {
                        // Right (x+): u = -z, v = +y
                        tangent   = glm::vec3(0.0f, 0.0f, -1.0f);
                        bitangent = glm::vec3(0.0f, 1.0f, 0.0f);
                    } else { // n.x < -0.5f, Left (x-)
                        // Left (x-): u = +z, v = +y
                        tangent   = glm::vec3(0.0f, 0.0f, 1.0f);
                        bitangent = glm::vec3(0.0f, 1.0f, 0.0f);
                    }


                    const int   GRID_RES  = 10;
                    const float halfSize  = 0.5f;
                    const float padding   = 1.0f;
                    const float step      = (2.0f * halfSize * padding) / float(GRID_RES);

                    const float sigmaWorld = 0.05f;
                    glm::vec3   scale      = glm::vec3(sigmaWorld);

                    glm::vec3 baseColor(1.0f);
                    switch (currentType) {
                        case BlockType::Grass:  baseColor = glm::vec3(0.3f, 0.8f, 0.3f); break;
                        case BlockType::Dirt:   baseColor = glm::vec3(0.4f, 0.3f, 0.2f); break;
                        case BlockType::Stone:  baseColor = glm::vec3(0.6f, 0.6f, 0.6f); break;
                        case BlockType::Sand:   baseColor = glm::vec3(0.9f, 0.85f, 0.6f); break;
                        case BlockType::Water:  baseColor = glm::vec3(0.2f, 0.4f, 0.8f); break;
                        case BlockType::Leaves: baseColor = glm::vec3(0.4f, 0.9f, 0.4f); break;
                        case BlockType::Wood:   baseColor = glm::vec3(0.5f, 0.3f, 0.15f); break;
                        default: break;
                    }

                    for (int iy = 0; iy < GRID_RES; ++iy) {
                        for (int ix = 0; ix < GRID_RES; ++ix) {
                            // Grid in [-halfSize*padding, +halfSize*padding]
                            float u = -halfSize * padding + (ix + 0.5f) * step;
                            float v = -halfSize * padding + (iy + 0.5f) * step;

                            glm::vec3 pos =
                                    center +
                                    n * surfaceOffset +
                                    tangent   * u +
                                    bitangent * v;

                            Splat s;
                            s.position = pos;
                            s.scale    = scale;
                            s.normal   = n;
                            s.color    = baseColor;
                            s.opacity  = 1.0f;

                            float texU = (ix + 0.5f) / GRID_RES;
                            float texV = (iy + 0.5f) / GRID_RES;
                            s.uv    = glm::vec2(texU, texV);
                            s.layer = BlockDatabase::GetTextureLayer(currentType, blockFace);

                            m_Splats.push_back(s);
                        }
                    }
                };

                // y+ (top)
                {
                    BlockState up = GetBlock(x, y + 1, z);
                    // For water: only surface (block above must not be water)
                    if (currentType == BlockType::Water) {
                        if (BlockDatabase::IsTransparent(up) && up.type != BlockType::Water) {
                            emitFaceSplats(glm::vec3(0.0f, 1.0f, 0.0f), BlockFace::Top);
                        }
                    } else {
                        if (neighborIsTransparent(up)) {
                            emitFaceSplats(glm::vec3(0.0f, 1.0f, 0.0f), BlockFace::Top);
                        }
                    }
                }
                // Water: surface-only, skip bottom/sides entirely
                if (currentType == BlockType::Water) {
                    continue;
                }
                // y- (bottom)
                if (neighborIsTransparent(GetBlock(x, y - 1, z))) {
                    emitFaceSplats(glm::vec3(0.0f, -1.0f, 0.0f), BlockFace::Bottom);
                }

                // z+ (front)
                {
                    BlockState n;
                    if (z + 1 >= CHUNK_WIDTH) {
                        if (Chunk* c = world.getChunk(cx, cz + 1)) {
                            n = c->GetBlock(x, y, 0);
                        } else {
                            n = BlockState(BlockType::Air);
                        }
                    } else {
                        n = GetBlock(x, y, z + 1);
                    }
                    if (neighborIsTransparent(n)) {
                        emitFaceSplats(glm::vec3(0.0f, 0.0f, 1.0f), BlockFace::Side);
                    }
                }

                // z- (back)
                {
                    BlockState n;
                    if (z - 1 < 0) {
                        if (Chunk* c = world.getChunk(cx, cz - 1)) {
                            n = c->GetBlock(x, y, CHUNK_WIDTH - 1);
                        } else {
                            n = BlockState(BlockType::Air);
                        }
                    } else {
                        n = GetBlock(x, y, z - 1);
                    }
                    if (neighborIsTransparent(n)) {
                        emitFaceSplats(glm::vec3(0.0f, 0.0f, -1.0f), BlockFace::Side);
                    }
                }

                // x+ (right)
                {
                    BlockState n;
                    if (x + 1 >= CHUNK_WIDTH) {
                        if (Chunk* c = world.getChunk(cx + 1, cz)) {
                            n = c->GetBlock(0, y, z);
                        } else {
                            n = BlockState(BlockType::Air);
                        }
                    } else {
                        n = GetBlock(x + 1, y, z);
                    }
                    if (neighborIsTransparent(n)) {
                        emitFaceSplats(glm::vec3(1.0f, 0.0f, 0.0f), BlockFace::Side);
                    }
                }

                // x- (left)
                {
                    BlockState n;
                    if (x - 1 < 0) {
                        if (Chunk* c = world.getChunk(cx - 1, cz)) {
                            n = c->GetBlock(CHUNK_WIDTH - 1, y, z);
                        } else {
                            n = BlockState(BlockType::Air);
                        }
                    } else {
                        n = GetBlock(x - 1, y, z);
                    }
                    if (neighborIsTransparent(n)) {
                        emitFaceSplats(glm::vec3(-1.0f, 0.0f, 0.0f), BlockFace::Side);
                    }
                }
            }
        }
    }
}
