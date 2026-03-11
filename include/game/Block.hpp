#pragma once

#include <cstdint>
#include <map>

// Simple block type enum
enum class BlockType : uint8_t {
    Air = 0,
    Stone = 1,
    Dirt = 2,
    Grass = 3,
    Sand = 4,
    Snow = 5,
    Water = 6,
    Wood = 7,
    Leaves = 8
};


struct BlockState {
    BlockType type;

    BlockState() : type(BlockType::Air) {}
    explicit BlockState(const BlockType type): type(type) {}

    static BlockState getBasic(const BlockType type) { return BlockState(type); }
};

// Enum to identify which face of a block we're talking about
enum class BlockFace {
    Top,
    Bottom,
    Side
};

// Texture layers that are not tied to a block type
enum class OverlayTexture {
    ShortGrass
};

/**
 * @struct BlockTextureInfo
 * @brief Stores the texture layer index for each face of a block.
 */
struct BlockTextureInfo {
    int top;
    int bottom;
    int side;
};

/**
 * @class BlockDatabase
 * @brief A static database for retrieving properties of each BlockType.
 *
 * This class maps BlockType enums to their properties, like texture IDs.
 */
class BlockDatabase {
public:
    /**
     * @brief Initializes the block database. Must be called once at startup.
     */
    static void Init();

    /**
     * @brief Gets the texture layer index for a specific face of a block.
     * @param type The BlockType (e.g., Grass, Dirt).
     * @param face The face to get the texture for (Top, Bottom, Side).
     * @return The integer index of the texture in the 2D texture array.
     */
    static float GetTextureLayer(BlockType type, BlockFace face);

    /**
     * @brief Gets a texture layer for overlay elements like grass blades.
     */
    static float GetOverlayTextureLayer(OverlayTexture overlay);

    /**
     * @brief Checks if a block type is transparent.
     * @param type The BlockType.
     * @return True if the block is Air or other transparent block.
     */
    static bool IsTransparent(BlockType type);
    static bool IsTransparent(BlockState block);

private:
    // Maps a BlockType to its set of texture indices (top, bottom, side)
    static std::map<BlockType, BlockTextureInfo> m_BlockTextures;
};
