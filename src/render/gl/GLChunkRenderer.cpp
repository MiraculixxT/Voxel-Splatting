#include "render/gl/GLChunkRenderer.hpp"

GLChunkRenderer::GLChunkRenderer() {
    // Maybe useful later
}

GLChunkRenderer::~GLChunkRenderer() {
    // Iterate through the map and delete all GL objects
    for (auto& pair : m_ChunkMeshes) {
        glDeleteBuffers(1, &pair.second.VBO);
        glDeleteVertexArrays(1, &pair.second.VAO);
    }
    m_ChunkMeshes.clear();
}

void GLChunkRenderer::UploadMesh(int x, int y, const std::vector<float>& vertices) {
    ChunkCoord coord = {x, y};

    // Find or create the mesh entry
    ChunkMesh& mesh = m_ChunkMeshes[coord];

    // If VAO/VBO haven't been generated yet (it's a new entry), generate them.
    if (mesh.VAO == 0) {
        glGenVertexArrays(1, &mesh.VAO);
    }
    if (mesh.VBO == 0) {
        glGenBuffers(1, &mesh.VBO);
    }

    if (vertices.empty()) {
        mesh.vertexCount = 0;
        return;
    }

    // Cast to int to match vertexCount type, std::vector::size() returns size_t
    mesh.vertexCount = static_cast<int>(vertices.size()) / 6; // 6 floats per vertex (x,y,z,u,v,layer)

    glBindVertexArray(mesh.VAO);

    glBindBuffer(GL_ARRAY_BUFFER, mesh.VBO);
    // Use glBufferData to upload new data, replacing any old data
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    // Vertex attribute layout (same as before):
    int stride = 6 * sizeof(float);
    // position (vec3) - location 0
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);
    // texture coords (vec2) - location 1
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // texture layer (float) - location 2
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(2);


    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void GLChunkRenderer::RemoveMesh(int x, int y) {
    ChunkCoord coord = {x, y};
    auto it = m_ChunkMeshes.find(coord);

    if (it != m_ChunkMeshes.end()) {
        // Delete the OpenGL buffers.
        ChunkMesh& mesh = it->second;
        glDeleteBuffers(1, &mesh.VBO);
        glDeleteVertexArrays(1, &mesh.VAO);

        // Remove the entry from the map
        m_ChunkMeshes.erase(it);
    }
}

void GLChunkRenderer::RemoveAllMeshes() {
    // Iterate through the map and delete all GL objects
    for (auto& pair : m_ChunkMeshes) {
        glDeleteBuffers(1, &pair.second.VBO);
        glDeleteVertexArrays(1, &pair.second.VAO);
    }
    m_ChunkMeshes.clear();
}


void GLChunkRenderer::Render() const {
    // Iterate over all stored meshes and render them
    for (const auto& pair : m_ChunkMeshes) {
        const ChunkMesh& mesh = pair.second;

        // Skip rendering if the chunk is empty
        if (mesh.vertexCount == 0) continue;

        glBindVertexArray(mesh.VAO);
        glDrawArrays(GL_TRIANGLES, 0, mesh.vertexCount);
    }
    // Unbind VAO once after all draw calls are done
    glBindVertexArray(0);
}

int GLChunkRenderer::GetTotalVertexCount() const {
    int total = 0;
    for (const auto& pair : m_ChunkMeshes) {
        total += pair.second.vertexCount;
    }
    return total;
}
