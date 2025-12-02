#pragma once

#include "glad/glad.h" // REQUIRED for OpenGL to function, even if not used directly here
#include <GLFW/glfw3.h>

#include "game/Settings.hpp"
#include "render/core/Camera.hpp"
#include "game/World.hpp"
#include <unordered_map>

#include "game/Player.hpp"

#include "render/gl/GLWorldRenderer.hpp"

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
    void UpdateTick();
    void Render();

    // Callbacks
    static void OnFramebufferSize(int width, int height);
    void OnMouseMove(double xpos, double ypos);

    // Static callback wrappers
    static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void MouseCallback(GLFWwindow* window, double xpos, double ypos);

    // Single key press detection
    bool WasKeyPressed(int key);
    std::unordered_map<int, int> m_LastKeyStates;

    GLFWwindow* m_Window;
    Camera m_Camera;

    // Timing
    float m_DeltaTime;
    float m_LastFrame;

    // Mouse state
    float m_LastX;
    float m_LastY;
    bool m_FirstMouse;
    bool m_InCamera = false;

    // World
    World m_World;

    // Rendering
    GLWorldRenderer m_WorldRenderer;

    // Settings
    Settings m_Settings;
    Player *m_Player;
};
