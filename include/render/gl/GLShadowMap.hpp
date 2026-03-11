#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>

class GLShadowMap {
public:
    GLShadowMap();
    ~GLShadowMap();

    bool init(int width, int height);

    void bindForWrite();
    void unbind();

    void bindForRead(GLenum textureUnit) const;

    GLuint getDepthTexture() const { return depthTexture; }
    const glm::mat4& getLightViewProj() const { return lightViewProj; }
    void setLightViewProj(const glm::mat4& m) { lightViewProj = m; }

private:
    GLuint fbo = 0;
    GLuint depthTexture = 0;
    int width = 0, height = 0;
    glm::mat4 lightViewProj{1.0f};
};