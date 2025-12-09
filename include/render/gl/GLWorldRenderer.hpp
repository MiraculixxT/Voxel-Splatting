#pragma once
#include "GLChunkRenderer.hpp"
#include "GLShader.hpp"
#include "game/Settings.hpp"
#include "game/World.hpp"
#include "render/core/Camera.hpp"
#include "GLSplatRenderer.hpp"
#include "GLShadowMap.hpp"

class GLWorldRenderer {
public:
    GLWorldRenderer(Camera& camera, Settings& settings, World& world)
        : m_Camera(camera), m_Settings(settings), m_World(world) {}
    ~GLWorldRenderer();

    void Init();
    void RenderWorld();
    GLChunkRenderer* GetChunkRenderer() const { return m_ChunkRenderer; }
    GLSplatRenderer* GetSplatRenderer() const { return m_SplatRenderer; }

    void RenderShadowPass();

private:
    Camera& m_Camera;
    Settings& m_Settings;
    World& m_World;
    GLChunkRenderer* m_ChunkRenderer = nullptr;
    GLSplatRenderer* m_SplatRenderer = nullptr;

    // Shadow + Light
    GLShadowMap m_ShadowMap;
    GLShader* m_ShadowShader = nullptr;
    glm::mat4 m_LightViewProj;

    GLShader* m_BlockShader = nullptr;
    unsigned int m_TextureArray = 0;

};
