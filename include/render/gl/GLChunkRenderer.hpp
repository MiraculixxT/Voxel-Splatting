#pragma once

#include <glad/glad.h>
#include <vector>
#include <map>
#include <utility>

class GLChunkRenderer {
public:
    // Define a coordinate type for the map key
    using ChunkCoord = std::pair<int, int>;

    GLChunkRenderer();
    ~GLChunkRenderer();

    /**
     * @brief Uploads mesh data for a specific chunk coordinate.
     * If a mesh for these coordinates already exists, it will be overridden.
     * @param x The chunk's x coordinate.
     * @param y The chunk's y coordinate.
     * @param vertices The vertex data.
     */
    void UploadMesh(int x, int y, const std::vector<float>& vertices);

    /**
     * @brief Removes the mesh data for a specific chunk coordinate.
     * @param x The chunk's x coordinate.
     * @param y The chunk's y coordinate.
     */
    void RemoveMesh(int x, int y);

    /**
     * Removes all meshes managed by this renderer for a fresh rebuild.
     */
    void RemoveAllMeshes();

    /**
     * @brief Renders all loaded chunk meshes.
     */
    void Render() const;

    /**
     * @brief Gets the total number of vertices in all managed chunk meshes.
     */
    int GetTotalVertexCount() const;

private:
    // Struct to hold OpenGL buffer info for a single chunk
    struct ChunkMesh {
        GLuint VAO = 0;
        GLuint VBO = 0;
        int vertexCount = 0;
    };

    // Map to store meshes by coordinate
    std::map<ChunkCoord, ChunkMesh> m_ChunkMeshes;
};
