#pragma once

#include <glad/glad.h>
#include <vector>
#include <utility>
#include <unordered_map>
#include <functional>

#include "game/Settings.hpp"
#include "render/core/Camera.hpp"
#include "render/core/ViewFrustum.hpp"

class GLChunkRenderer {
public:
    // Define a coordinate type for the map key
    using ChunkCoord = std::pair<int, int>;

    explicit GLChunkRenderer(Camera& camera, Settings& settings);
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
    void Render(const ViewFrustum &frustum, int fromX, int toX, int fromZ, int toZ);

    /**
     * @brief Gets the total number of vertices in all managed chunk meshes.
     */
    std::size_t GetTotalVertexCount() const;

private:
    // Struct to hold OpenGL buffer info for a single chunk
    struct ChunkMesh {
        GLuint VAO = 0;
        GLuint VBO = 0;
        int vertexCount = 0;
    };

    // Hash for the unordered map key
    struct ChunkCoordHash {
        std::size_t operator()(const std::pair<int, int>& k) const {
            // Boost-like pattern hash
            return std::hash<int>{}(k.first) ^ (std::hash<int>{}(k.second) + 0x9e3779b9 + (std::hash<int>{}(k.first) << 6) + (std::hash<int>{}(k.first) >> 2));
        }
    };

    // Map to store meshes by coordinate
    std::unordered_map<ChunkCoord, ChunkMesh, ChunkCoordHash> m_ChunkMeshes;

    // Keeps track of the total loaded vertex count
    std::size_t m_loadedVertexCount = 0;

    Camera& m_Camera;
    Settings& m_Settings;
};
