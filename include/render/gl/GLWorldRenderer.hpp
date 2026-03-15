#pragma once
#include "GLChunkRenderer.hpp"
#include "GLShader.hpp"
#include "game/Settings.hpp"
#include "game/World.hpp"
#include "render/core/Camera.hpp"
#include "GLSplatRenderer.hpp"
#include "GLShadowMap.hpp"
#include "happly.hpp"

class GLWorldRenderer {
public:
    GLWorldRenderer(Camera& camera, Settings& settings, World& world)
        : m_Camera(camera), m_Settings(settings), m_World(world) {}
    ~GLWorldRenderer();

    struct SplatData {
        glm::vec3 pos;
        float scale[3];
        float rot[4];    // Quaternion (xyzw)
        uint8_t color[4]; // RGBA
    };

    struct SortItem {
        float distance;
        int index;
    };

    void Init();

    void InitializeSplats(happly::PLYData &plyIn);

    void RenderWorld();
    GLChunkRenderer* GetChunkRenderer() const { return m_ChunkRenderer; }
    GLSplatRenderer* GetSplatRenderer() const { return m_SplatRenderer; }

    void RenderShadowPass();

    // PLY
    std::vector<SplatData> m_SplatRawData;
    std::vector<SplatData> m_SortedSplatData; // Data to be uploaded to GPU
    std::vector<SortItem> m_SortList;
    unsigned int m_SplatVBO, m_SplatVAO;

private:
    Camera& m_Camera;
    Settings& m_Settings;
    World& m_World;
    GLChunkRenderer* m_ChunkRenderer = nullptr;
    GLSplatRenderer* m_SplatRenderer = nullptr;

    // Shadow + Light
    GLShadowMap m_ShadowMap;
    GLShader* m_ShadowShader = nullptr;
    glm::vec3 m_SunDir;          // Direction towards the sun in world space
    glm::mat4 m_LightViewProj;

    GLShader* m_BlockShader = nullptr;
    GLShader* m_GrassShader = nullptr;
    GLShader* m_SplatShader = nullptr;
    unsigned int m_TextureArray = 0;

    // Sky rendering
    GLShader* m_SkyShader = nullptr;
    unsigned int m_SkyVAO = 0;

    GLShader* m_SunFlareShader = nullptr;

    // Godrays
    GLShader* m_GodrayShader = nullptr;
    GLuint m_GodrayOcclusionFBO = 0;
    GLuint m_GodrayOcclusionTex = 0;
    GLShader* m_GodrayOcclusionShader = nullptr;

    glm::vec3 m_LastSortPos;
};
