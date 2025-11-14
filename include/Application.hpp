#pragma once

#include "glad/glad.h" // REQUIRED for OpenGL to function, even if not used directly here
#include <GLFW/glfw3.h>

#include "render/core/Camera.hpp"
#include "game/World.hpp"
#include "render/gl/GLShader.hpp"
#include "render/gl/GLChunkRenderer.hpp"

// Constants
const unsigned int SCR_WIDTH = 1280;
const unsigned int SCR_HEIGHT = 720;

class Application {
public:
    Application();
    ~Application();

    void Init();
    void Run();

private:
    void ProcessInput();
    void Update();
    void Render();

    // Callbacks
    void OnFramebufferSize(int width, int height);
    void OnMouseMove(double xpos, double ypos);

    // Static callback wrappers
    static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void MouseCallback(GLFWwindow* window, double xpos, double ypos);

    GLFWwindow* m_Window;
    Camera m_Camera;

    // Timing
    float m_DeltaTime;
    float m_LastFrame;

    // Mouse state
    float m_LastX;
    float m_LastY;
    bool m_FirstMouse;

    // World
    World world;

    // Rendering
    GLShader* m_BlockShader;
    GLChunkRenderer* m_ChunkRenderer;
    unsigned int m_TextureArray;

    // Render layering
    float m_GLFrom;
    float m_GLTo;
};
