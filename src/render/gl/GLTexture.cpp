#include "render/gl/GLTexture.hpp"
#include "stb_image.h"
#include <iostream>

GLTexture::GLTexture(const std::string& path, FilterMode filterMode)
    : m_TextureID(0), m_FilePath(path), m_Width(0), m_Height(0), m_Channels(0), m_FilterMode(filterMode) {}

GLTexture::~GLTexture() {
    if (m_TextureID != 0) {
        glDeleteTextures(1, &m_TextureID);
    }
}

bool GLTexture::Load() {
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(m_FilePath.c_str(), &m_Width, &m_Height, &m_Channels, 0);
    if (data) {
        GLenum format;
        if (m_Channels == 1)
            format = GL_RED;
        else if (m_Channels == 3)
            format = GL_RGB;
        else if (m_Channels == 4)
            format = GL_RGBA;

        glGenTextures(1, &m_TextureID);
        glBindTexture(GL_TEXTURE_2D, m_TextureID);
        glTexImage2D(GL_TEXTURE_2D, 0, format, m_Width, m_Height, 0, format, GL_UNSIGNED_BYTE, data);

        if (m_FilterMode == FilterMode::PixelPerfect) {
            // Pixel-perfect UI / overlay textures: no mipmaps, nearest filtering
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        } else {
            // Default: world textures with smooth filtering and mipmaps
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }

        stbi_image_free(data);
        return true;
    } else {
        std::cout << "Texture failed to load at path: " << m_FilePath << std::endl;
        stbi_image_free(data);
        return false;
    }
}

void GLTexture::Bind(GLenum textureUnit) const {
    glActiveTexture(textureUnit);
    glBindTexture(GL_TEXTURE_2D, m_TextureID);
}

void GLTexture::Unbind() const {
    glBindTexture(GL_TEXTURE_2D, 0);
}
