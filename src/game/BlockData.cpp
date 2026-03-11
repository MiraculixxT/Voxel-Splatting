#include <map>
#include "game/Block.hpp"

// Initialize static member
std::map<BlockType, BlockTextureInfo> BlockDatabase::m_BlockTextures;

void BlockDatabase::Init() {
    // This mapping MUST match the order of files in Application::Init
    // 0 = stone.png
    // 1 = dirt.png
    // 2 = grass_top.png
    // 3 = grass_side.png
    // 4 = sand.png
    // 5 = snow.png
    // 6 = water.png
    // 7 = log_side.png
    // 8 = log_top.png
    // 9 = leave.png
    // 10 = short_grass.png

    m_BlockTextures[BlockType::Stone] = {0, 0, 0}; // Top, Bottom, Side
    m_BlockTextures[BlockType::Dirt]  = {1, 1, 1};
    m_BlockTextures[BlockType::Grass] = {2, 1, 3}; // Top=grass_top, Bottom=dirt, Side=grass_side
    m_BlockTextures[BlockType::Sand]  = {4, 4, 4};
    m_BlockTextures[BlockType::Snow]  = {5, 1, 5}; // Top=snow, Bottom=dirt, Side=snow
    m_BlockTextures[BlockType::Water] = {6, 6, 6}; // Placeholder for now
    m_BlockTextures[BlockType::Wood]  = {8, 8, 7}; // Top=log_top, Bottom=log_top, Side=log_side
    m_BlockTextures[BlockType::Leaves]= {9, 9, 9}; //
}

float BlockDatabase::GetTextureLayer(BlockType type, BlockFace face) {
    auto it = m_BlockTextures.find(type);
    if (it == m_BlockTextures.end()) {
        return 0.0f; // Default to stone if not found
    }

    switch (face) {
        case BlockFace::Top:    return (float)it->second.top;
        case BlockFace::Bottom: return (float)it->second.bottom;
        case BlockFace::Side:   return (float)it->second.side;
    }
    return 0.0f;
}

float BlockDatabase::GetOverlayTextureLayer(OverlayTexture overlay) {
    switch (overlay) {
        case OverlayTexture::ShortGrass:
            return 10.0f;
    }
    return 0.0f;
}

bool BlockDatabase::IsTransparent(const BlockType type) {
    return type == BlockType::Air || type == BlockType::Water || type == BlockType::Leaves;
}

bool BlockDatabase::IsTransparent(const BlockState block) {
    return IsTransparent(block.type);
}
