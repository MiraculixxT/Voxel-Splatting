#pragma once
#include "GLChunkRenderer.hpp"
#include "GLShader.hpp"
#include "game/Settings.hpp"
#include "game/World.hpp"
#include "render/core/Camera.hpp"

class GLWorldRenderer {
public:
    GLWorldRenderer(Camera& camera, Settings& settings, World& world)
        : m_Camera(camera), m_Settings(settings), m_World(world) {}
    ~GLWorldRenderer();

    void Init();
    void RenderWorld();
    GLChunkRenderer* GetChunkRenderer() const { return m_ChunkRenderer; }

private:
    Camera& m_Camera;
    Settings& m_Settings;
    World& m_World;
    GLChunkRenderer* m_ChunkRenderer = nullptr;

    GLShader* m_BlockShader = nullptr;
    unsigned int m_TextureArray = 0;

};
