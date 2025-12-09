#include "render/gl/GLTextureUtils.hpp"

#include <glad/glad.h>
#include <iostream>

#define STB_IMAGE_IMPLEMENTATION
#include <vector>
#include <string>
#include "stb_image.h"

unsigned int GLTextureUtils::LoadTexture2DArray(const std::vector<std::string>& textureFiles) {
    if (textureFiles.empty()) {
        std::cerr << "TEXTURE_ARRAY: No files provided." << std::endl;
        return 0;
    }

    stbi_set_flip_vertically_on_load(true); // Flip textures vertically

    int width = 0, height = 0, nrChannels = 0;
    const int desiredChannels = 4; // always load as RGBA
    int layerCount = static_cast<int>(textureFiles.size());
    unsigned char* first_data = nullptr;

    // --- 1. Load first image to get dimensions --- TODO: Allow different sizes with scaling?
    first_data = stbi_load(textureFiles[0].c_str(), &width, &height, &nrChannels, desiredChannels);
    if (!first_data) {
        std::cerr << "TEXTURE_ARRAY: Failed to load first image: " << textureFiles[0] << std::endl;
        return 0;
    }

    GLenum format = GL_RGBA;

    // --- 2. Create Texture Array ---
    unsigned int textureArrayID;
    glGenTextures(1, &textureArrayID);
    glBindTexture(GL_TEXTURE_2D_ARRAY, textureArrayID);

    // Allocate storage for the whole array
    glTexImage3D(GL_TEXTURE_2D_ARRAY,
        0,                 // mipmap level
        GL_RGBA8,          // internal format (8-bit RGBA)
        width,             // width
        height,            // height
        layerCount,        // number of layers
        0,                 // border
        GL_RGBA,           // format
        GL_UNSIGNED_BYTE,  // type
        NULL);             // data (we'll load it layer by layer)

    // --- 3. Load first image into layer 0 ---
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY,
        0,                 // mipmap level
        0, 0, 0,           // x, y, z offsets (z is layer)
        width, height, 1,  // width, height, depth (1 layer)
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        first_data);
    stbi_image_free(first_data);

    // --- 4. Load remaining images into other layers ---
    for (int i = 1; i < layerCount; ++i) {
        int imgWidth, imgHeight, imgChannels;
        unsigned char* data = stbi_load(textureFiles[i].c_str(), &imgWidth, &imgHeight, &imgChannels, desiredChannels);
        if (!data) {
            std::cerr << "TEXTURE_ARRAY: Failed to load image: " << textureFiles[i] << std::endl;
            continue;
        }

        if (imgWidth != width || imgHeight != height) {
            std::cerr << "TEXTURE_ARRAY: Image " << textureFiles[i] << " has different dimensions!" << std::endl;
            stbi_image_free(data);
            continue;
        }

        glTexSubImage3D(GL_TEXTURE_2D_ARRAY,
            0,
            0, 0, i,           // z offset is now 'i'
            width, height, 1,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            data);

        stbi_image_free(data);
    }

    // --- 5. Set texture parameters ---
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    // Use nearest neighbor filtering for blocky pixel art
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);

    return textureArrayID;
}