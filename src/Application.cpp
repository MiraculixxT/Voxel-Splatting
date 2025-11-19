#include "Application.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "glm/vec3.hpp"
#include "render/core/Camera.hpp"
#include "render/core/GUIRenderer.hpp"
#include "render/gl/GLChunkRenderer.hpp"
#include "game/Player.hpp"

Application::Application()
    : m_Window(nullptr),
      m_Camera(glm::vec3(8.0f, 50.0f, 24.0f)),
      m_DeltaTime(0.0f),
      m_LastFrame(0.0f),
      m_LastX(SCR_WIDTH / 2.0f),
      m_LastY(SCR_HEIGHT / 2.0f),
      m_FirstMouse(true),
      m_WorldRenderer(m_Camera, m_Settings, m_World),
      m_Player(nullptr)
      {
}

Application::~Application() {
    // --- Cleanup ---
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (m_Window) {
        glfwDestroyWindow(m_Window);
    }
    glfwTerminate();
}

void Application::Init() {
    // --- 1. Initialize GLFW ---
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__ // Dear macOS aka Apple... UPDATE YOUR SYSTEM!
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    // --- 2. Create Window ---
    m_Window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Voxel Game", nullptr, nullptr);
    if (m_Window == nullptr) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }
    glfwMakeContextCurrent(m_Window);
    glfwSwapInterval(1);

    // Store `this` pointer to be retrievable from C-style callbacks
    glfwSetWindowUserPointer(m_Window, this);
    glfwSetFramebufferSizeCallback(m_Window, FramebufferSizeCallback);
    glfwSetCursorPosCallback(m_Window, MouseCallback);

    // --- 3. Load GLAD ---
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        throw std::runtime_error("Failed to initialize GLAD");
    }

    // --- 4. Configure OpenGL State ---
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    // --- 5. Setup ImGui ---
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(m_Window, true);
    ImGui_ImplOpenGL3_Init("#version 410");

    // --- 6. Initialize BlockData ---
    BlockDatabase::Init();

    // --- 7. Initialize World Renderer ---
    m_WorldRenderer.Init();
    m_Player = new Player(&m_Camera, &m_World);
}

void Application::Run() {
    // --- 8. Main Render Loop ---
    while (!glfwWindowShouldClose(m_Window)) {
        // --- Per-frame logic ---
        float currentFrame = glfwGetTime();
        m_DeltaTime = currentFrame - m_LastFrame;
        m_LastFrame = currentFrame;

        // --- Input ---
        ProcessInput();

        // --- Update ---
        Update();

        // --- Render ---
        Render();

        // --- Swap Buffers and Poll Events ---
        glfwSwapBuffers(m_Window);
        glfwPollEvents();
    }
}

bool Application::WasKeyPressed(int key) {
    const int current = glfwGetKey(m_Window, key);

    int last = GLFW_RELEASE;
    auto it = m_LastKeyStates.find(key);
    if (it != m_LastKeyStates.end()) {
        last = it->second;
    }

    m_LastKeyStates[key] = current;

    // trigger only on edge: RELEASE -> PRESS
    return (current == GLFW_PRESS && last == GLFW_RELEASE);
}

void Application::ProcessInput() {
    if (WasKeyPressed(GLFW_KEY_ESCAPE))
        m_InCamera = !m_InCamera;

    if (m_Player)
        m_Player->ProcessInput(m_Window, m_DeltaTime);
}

void Application::Update() {
    if (m_Player)
        m_Player->Update(m_DeltaTime);
    // world ticking
}

void Application::Render() {
    // --- Start ImGui Frame ---
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // --- Render World ---
    m_WorldRenderer.RenderWorld();

    // --- Draw ImGui UI ---
    float xscale, yscale;
    glfwGetWindowContentScale(m_Window, &xscale, &yscale);
    ImGui::GetIO().FontGlobalScale = xscale;

    auto& chunkRenderer = m_WorldRenderer.m_ChunkRenderer;
    GUIRenderer::RenderStatsOverview(chunkRenderer->GetTotalVertexCount(), m_Camera, m_Settings);
    if (!m_InCamera) GUIRenderer::RenderSettingsScreen(m_Settings, m_Camera, chunkRenderer, m_World);
    ImGui::Render();

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// --- Static Callback Wrappers ---

void Application::FramebufferSizeCallback(GLFWwindow* window, int width, int height) {
    Application* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
    if (app) {
        app->OnFramebufferSize(width, height);
    }
}

void Application::MouseCallback(GLFWwindow* window, double xpos, double ypos) {
    Application* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
    if (app) {
        app->OnMouseMove(xpos, ypos);
    }
}

// --- Member-function Callback Implementations ---

void Application::OnFramebufferSize(int width, int height) {
    glViewport(0, 0, width, height);
}

void Application::OnMouseMove(double xpos, double ypos) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) {
        m_FirstMouse = true;
        return;
    }

    // Capture mouse inputs: glfwGetMouseButton(m_Window, GLFW_MOUSE_BUTTON_RIGHT) != GLFW_PRESS

    if (!m_InCamera) {
        m_FirstMouse = true;
        glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        return;
    }

    glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    if (m_FirstMouse) {
        m_LastX = xpos;
        m_LastY = ypos;
        m_FirstMouse = false;
    }

    float xoffset = xpos - m_LastX;
    float yoffset = m_LastY - ypos;

    m_LastX = xpos;
    m_LastY = ypos;

    m_Camera.ProcessMouseMovement(xoffset, yoffset);
}