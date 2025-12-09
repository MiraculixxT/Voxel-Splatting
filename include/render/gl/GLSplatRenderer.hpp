#pragma once

#include <map>
#include <utility>
#include <vector>
#include <glad/glad.h>
#include <glm/glm.hpp>

#include "game/Chunk.hpp"


class GLSplatRenderer {
public:
    GLSplatRenderer();
    ~GLSplatRenderer();

    /**
     * @brief Uploads all splats belonging to a chunk (cx, cz) to the GPU.
     */
    void UploadSplats(int cx, int cz, const std::vector<Splat>& splats);

    /**
     * @brief Removes all GPU data belonging to chunk (cx, cz).
     */
    void RemoveSplats(int cx, int cz);

    /**
     * @brief Draws all uploaded splats.
     * @param viewProj Combined view-projection matrix.
     */
    void Draw(const glm::mat4& viewProj, int camChunkX, int camChunkZ, int maxChunkDistance);

    // Configure lighting and shadow mapping for splats
    void SetLighting(const glm::mat4& lightViewProj, GLuint shadowTexture, const glm::vec3& lightDir);

private:
    struct ChunkSplatBuffer {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLsizei count = 0;
    };

    // Key = (cx, cz)
    std::map<std::pair<int,int>, ChunkSplatBuffer> buffers;

    GLuint shaderProgram = 0;
    GLuint m_QuadVbo = 0;

    // Cached lighting/shadow parameters
    glm::mat4 m_LightViewProj {1.0f};
    GLuint m_ShadowTexture = 0;
    glm::vec3 m_LightDir { -0.3f, -1.0f, -0.2f };

    void initQuad();

    GLuint createShaderProgram();
    GLuint compileShader(GLenum type, const char* src);


};