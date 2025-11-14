#pragma once

#include <vector>
#include <string>

namespace GLTextureUtils {
    /**
     * @brief Loads a 2D Texture Array from a list of files.
     * All textures MUST be the SAME dimensions!!!!
     * @param textureFiles A vector of file paths to the textures.
     * @return The OpenGL texture ID for the 2D array.
     */
    unsigned int LoadTexture2DArray(const std::vector<std::string>& textureFiles);
}
