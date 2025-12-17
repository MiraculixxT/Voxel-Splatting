#pragma once

#include <string>
#include "glad/glad.h"

class GLTexture {
public:
    enum class FilterMode {
        LinearMipmap,
        PixelPerfect
    };

    GLTexture(const std::string& path, FilterMode filterMode = FilterMode::LinearMipmap);
    ~GLTexture();

    bool Load();
    void Bind(GLenum textureUnit = GL_TEXTURE0) const;
    void Unbind() const;

    int GetWidth() const { return m_Width; }
    int GetHeight() const { return m_Height; }

private:
    GLuint m_TextureID;
    std::string m_FilePath;
    int m_Width, m_Height, m_Channels;
    FilterMode m_FilterMode;
};
