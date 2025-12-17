#pragma once

#include "glad/glad.h"
#include "render/gl/GLTexture.hpp"
#include "render/gl/GLShader.hpp"
#include "glm/glm.hpp"
#include "game/Block.hpp"

class Player;

class GameOverlay {
public:
    GameOverlay();
    ~GameOverlay();

    void Init(int screenWidth, int screenHeight);
    void Render();
    void OnFramebufferSize(int width, int height);

    void SetPlayer(Player* player) { m_Player = player; }

private:
    void RenderCrosshair();
    void RenderHotbar();
    void RenderHotbarTextImGui();
    void RenderHotbarNumbers();
    void setupQuad();

    // Helper to get slot center in screen space
    void GetSlotCenter(int index, float &outX, float &outY) const;

    GLShader m_Shader;
    GLShader m_TextShader;
    GLTexture m_HotbarTexture;
    GLTexture m_SelectedSlotTexture;
    GLTexture m_NumberAtlasTexture;

    // Simple fixed mapping from BlockType enum to overlay icon textures
    static constexpr int kBlockTypeCount = 9; // matches highest BlockType value + 1
    GLTexture* m_BlockIconTextures[kBlockTypeCount];

    GLuint m_Vao;
    GLuint m_Vbo;
    GLuint m_Ebo;

    int m_Width;
    int m_Height;

    // Cached hotbar layout values for both GL and ImGui
    float m_HotbarScale = 1.0f;
    float m_HotbarWidth = 0.0f;
    float m_HotbarHeight = 0.0f;
    float m_HotbarX = 0.0f;
    float m_HotbarY = 0.0f;

    Player* m_Player = nullptr;
};
