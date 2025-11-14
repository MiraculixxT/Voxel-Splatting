#include "render/gl/GLChunkRenderer.hpp"

GLChunkRenderer::GLChunkRenderer() : m_VAO(0), m_VBO(0), m_VertexCount(0) {
    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);
}

GLChunkRenderer::~GLChunkRenderer() {
    glDeleteBuffers(1, &m_VBO);
    glDeleteVertexArrays(1, &m_VAO);
}

void GLChunkRenderer::UploadMesh(const std::vector<float>& vertices) {
    if (vertices.empty()) {
        m_VertexCount = 0;
        return;
    }

    m_VertexCount = vertices.size() / 6; // 6 floats per vertex (x,y,z,u,v,layer)

    glBindVertexArray(m_VAO);

    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    // Vertex attribute layout:
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

void GLChunkRenderer::Render() {
    if (m_VertexCount == 0) return;

    glBindVertexArray(m_VAO);
    glDrawArrays(GL_TRIANGLES, 0, m_VertexCount);
    glBindVertexArray(0);
}
