#pragma once

#include <map>
#include <utility>
#include <vector>
#include <string>
#include <glad/glad.h>
#include <glm/glm.hpp>

#include "game/Chunk.hpp"


class GLSplatRenderer {
public:
    GLSplatRenderer();
    ~GLSplatRenderer();

    // --- Trained splats loading ---
    // Loads all files named like `chunk_<cx>_<cz>.splats.bin` from a folder and uploads them.
    // Returns true if at least one chunk was loaded.
    bool LoadTrainedChunkFolder(const std::string& folder);

    // Loads a single chunk splat file and uploads it.
    bool LoadTrainedChunkFile(const std::string& path);

    // Clears all currently uploaded trained/runtime splats from GPU.
    void ClearAll();

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
        bool trained = false;
    };

    // Key = (cx, cz)
    std::map<std::pair<int,int>, ChunkSplatBuffer> buffers;

    GLuint shaderProgram = 0;
    GLuint m_QuadVbo = 0;

    // Cached lighting/shadow parameters
    glm::mat4 m_LightViewProj {1.0f};
    GLuint m_ShadowTexture = 0;
    glm::vec3 m_LightDir { -0.3f, -1.0f, -0.2f };

    // --- File IO helpers for trained splats ---
    struct SplatBinHeader {
        uint32_t magic = 0;   // expected 'SPL2'
        uint32_t version = 0; // expected 2
        int32_t cx = 0;
        int32_t cz = 0;
        uint32_t count = 0;
        uint32_t stride = 0;  // bytes per splat
    };

    // Reads a SPL2 header; returns false on EOF/corruption.
    static bool readHeader(std::ifstream& in, SplatBinHeader& h);

    // Converts raw float payload (stride can be 40 or 64 bytes) into engine `Splat` objects.
    // We conservatively read the first 10 floats as: pos3, scale3, color3, opacity.
    static void decodeToSplats(const SplatBinHeader& h, const std::vector<uint8_t>& raw, std::vector<Splat>& out);

    void initQuad();

    GLuint createShaderProgram();
    GLuint compileShader(GLenum type, const char* src);
};