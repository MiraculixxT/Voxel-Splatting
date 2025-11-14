#pragma once

#include <glad/glad.h>
#include <vector>

class GLChunkRenderer {
public:
    GLChunkRenderer();
    ~GLChunkRenderer();

    /**
     * @brief Uploads mesh data to the GPU.
     * @param vertices The vertex data (e.g., from Chunk::GetMeshVertices()).
     */
    void UploadMesh(const std::vector<float>& vertices);

    /**
     * @brief Renders the chunk mesh.
     */
    void Render();

    /**
     * @brief Gets the number of vertices in the current mesh.
     */
    int GetVertexCount() const { return m_VertexCount; }

private:
    GLuint m_VAO, m_VBO;
    int m_VertexCount;
};
