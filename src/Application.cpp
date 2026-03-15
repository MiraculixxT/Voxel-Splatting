#include "Application.hpp"

#include <iostream>
#include <stdexcept>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "glm/vec3.hpp"
#include "render/core/Camera.hpp"
#include "render/core/GUIRenderer.hpp"
#include "render/gl/GLChunkRenderer.hpp"
#include "game/Player.hpp"
#include "render/gl/happly.hpp"

#include "render/core/GameOverlay.hpp"

Application::Application()
    : m_Window(nullptr),
      m_Camera(glm::vec3(8.0f, 50.0f, 24.0f)),
      m_DeltaTime(0.0f),
      m_LastFrame(0.0f),
      m_LastX(SCR_WIDTH / 2.0f),
      m_LastY(SCR_HEIGHT / 2.0f),
      m_FirstMouse(true),
      m_InCamera(false),
      m_World(m_Settings),
      m_WorldRenderer(m_Camera, m_Settings, m_World),
      m_GameOverlay(),
      m_Settings(),
      m_Player(nullptr) {
}

Application::~Application() {
    // --- Cleanup ---
    delete m_Player;
    m_Player = nullptr;
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
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    // --- 6. Initialize BlockData ---
    BlockDatabase::Init();

    // --- 7. Initialize World Renderer ---
    m_WorldRenderer.Init();
    m_GameOverlay.Init(SCR_WIDTH, SCR_HEIGHT);
    m_Player = new Player(&m_Camera, &m_World);
    m_World.setPlayer(m_Player);
    m_World.setChunkRenderer(m_WorldRenderer.GetChunkRenderer());
    m_GameOverlay.SetPlayer(m_Player);

    // --- 8. FIX: Force Initial Viewport Update ---
    // Wayland/KDE often initializes with different Window vs Framebuffer sizes.
    int width, height;
    glfwGetFramebufferSize(m_Window, &width, &height);
    OnFramebufferSize(width, height);
}

void Application::Run() {
    const double dt = 1.0 / 20.0;
    double accumulator = 0.0;

    // --- Main Render Loop ---
    while (!glfwWindowShouldClose(m_Window)) {
        // --- Per-frame logic ---
        const double currentFrame = glfwGetTime();
        double frameTime = currentFrame - m_LastFrame;
        m_LastFrame = currentFrame;

        // Prevent spiral of death if frame time is too long
        if (frameTime > 0.25) frameTime = 0.25;

        m_DeltaTime = static_cast<float>(frameTime);
        accumulator += frameTime;

        // --- Input ---
        ProcessInput();

        // --- Fixed Tickrate (Physics & World Gen) ---
        while (accumulator >= dt) {
            UpdateTick();
            accumulator -= dt;
        }

        // --- Update as fast as possible ---
        Update();

        // --- Render ---
        // (Rendering happens as fast as possible, using interpolation if needed)
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
    if (WasKeyPressed(GLFW_KEY_ESCAPE)) {
        m_InCamera = !m_InCamera;
        Camera::SetCameraMode(m_InCamera, m_Window);
    }


    if (m_Player)
        m_Player->ProcessInput(m_Window, m_DeltaTime);
}

void Application::Update() {
    if (m_Player)
        m_Player->Update(m_DeltaTime);
}

void Application::UpdateTick() {
    m_World.tick();
}

void Application::Render() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m_WorldRenderer.RenderWorld();

    // --- Start ImGui Frame ---
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // --- Render World again? (kept as in original) ---
    m_WorldRenderer.RenderWorld();

    // --- Draw ImGui UI ---
    float xscale, yscale;
    glfwGetWindowContentScale(m_Window, &xscale, &yscale);
    ImGui::GetIO().FontGlobalScale = xscale;

    const auto& chunkRenderer = m_WorldRenderer.GetChunkRenderer();
    GUIRenderer::RenderStatsOverview(chunkRenderer->GetTotalVertexCount(), m_Camera, m_Settings);
    if (!m_InCamera) GUIRenderer::RenderSettingsScreen(m_Settings, m_Camera, chunkRenderer, m_World, m_Player);

    // Render overlay (crosshair, hotbar, etc.) after setting up ImGui
    m_GameOverlay.Render();

    ImGui::Render();

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// --- Static Callback Wrappers ---

void Application::FramebufferSizeCallback(GLFWwindow* window, const int width, const int height) {
    if (auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window))) {
        app->OnFramebufferSize(width, height);
    }
}

void Application::MouseCallback(GLFWwindow* window, const double xpos, const double ypos) {
    if (auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window))) {
        app->OnMouseMove(xpos, ypos);
    }
}

// --- Member-function Callback Implementations ---

void Application::OnFramebufferSize(const int width, const int height) {
    glViewport(0, 0, width, height);
    m_GameOverlay.OnFramebufferSize(width, height);
}

void Application::OnMouseMove(const double xpos, const double ypos) {
    // 1. If ImGui owns the mouse, do nothing
    if (ImGui::GetIO().WantCaptureMouse) return;

    // 2. If we aren't in camera mode, do nothing
    if (!m_InCamera) return;

    // 3. Standard Movement Logic
    if (m_FirstMouse) {
        m_LastX = xpos;
        m_LastY = ypos;
        m_FirstMouse = false;
    }

    const float xoffset = xpos - m_LastX;
    const float yoffset = m_LastY - ypos;

    m_LastX = xpos;
    m_LastY = ypos;

    m_Camera.ProcessMouseMovement(xoffset, yoffset);
}