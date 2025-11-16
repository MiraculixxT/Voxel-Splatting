#include "Application.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "glm/vec3.hpp"
#include "glm/ext/matrix_clip_space.hpp"
#include "render/core/Camera.hpp"
#include "render/core/GUIRenderer.hpp"
#include "render/gl/GLChunkRenderer.hpp"
#include "render/gl/GLTextureUtils.hpp"
#include "render/gl/GLShader.hpp"

Application::Application()
    : m_Window(nullptr),
      m_Camera(glm::vec3(8.0f, 50.0f, 24.0f)),
      m_DeltaTime(0.0f),
      m_LastFrame(0.0f),
      m_LastX(SCR_WIDTH / 2.0f),
      m_LastY(SCR_HEIGHT / 2.0f),
      m_FirstMouse(true),
      m_BlockShader(nullptr),
      m_ChunkRenderer(nullptr),
      m_TextureArray(0) {
}

Application::~Application() {
    // --- Cleanup ---
    delete m_BlockShader;
    delete m_ChunkRenderer;

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

    // --- 7. Build Shaders and Load Textures ---
    m_BlockShader = new GLShader("assets/shaders/block.vsh", "assets/shaders/block.fsh");

    // Define the texture order. This MUST match BlockData::Init()
    std::vector<std::string> textureFiles = {
        "assets/textures/stone.png",       // i 0
        "assets/textures/dirt.png",        // i 1
        "assets/textures/grass_top.png",   // i 2
        "assets/textures/grass_side.png"   // i 3
    };
    m_TextureArray = GLTextureUtils::LoadTexture2DArray(textureFiles);

    m_BlockShader->use();
    m_BlockShader->setInt("textureArray", 0);

    // --- 8. Create World/Chunk ---
    m_ChunkRenderer = new GLChunkRenderer();
    for (auto [cx, column] : m_World.getChunks()) {
        for (auto& [cy, chunk] : column) {
            chunk.BuildMesh(m_World);
            m_ChunkRenderer->UploadMesh(cx, cy, chunk.GetMeshVertices());
        }
    }
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

    if (glfwGetKey(m_Window, GLFW_KEY_W) == GLFW_PRESS)
        m_Camera.ProcessKeyboard(FORWARD, m_DeltaTime);
    if (glfwGetKey(m_Window, GLFW_KEY_S) == GLFW_PRESS)
        m_Camera.ProcessKeyboard(BACKWARD, m_DeltaTime);
    if (glfwGetKey(m_Window, GLFW_KEY_A) == GLFW_PRESS)
        m_Camera.ProcessKeyboard(LEFT, m_DeltaTime);
    if (glfwGetKey(m_Window, GLFW_KEY_D) == GLFW_PRESS)
        m_Camera.ProcessKeyboard(RIGHT, m_DeltaTime);
    if (glfwGetKey(m_Window, GLFW_KEY_SPACE) == GLFW_PRESS)
        m_Camera.ProcessKeyboard(UP, m_DeltaTime);
    if (glfwGetKey(m_Window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        m_Camera.ProcessKeyboard(DOWN, m_DeltaTime);
}

void Application::Update() {
    // world ticking
}

void Application::Render() {
    // --- Start ImGui Frame ---
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // --- Clear Screen ---
    glClearColor(0.5f, 0.8f, 1.0f, 1.0f); // Sky blue
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // --- Draw World ---
    m_BlockShader->use();
    glm::mat4 projection = glm::perspective(glm::radians(m_Camera.Zoom), (float)SCR_WIDTH / (float)SCR_HEIGHT, m_Settings.GLFrom, m_Settings.GLTo);
    glm::mat4 view = m_Camera.GetViewMatrix();
    glm::mat4 model = glm::mat4(1.0f);

    m_BlockShader->setMat4("projection", projection);
    m_BlockShader->setMat4("view", view);
    m_BlockShader->setMat4("model", model);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_TextureArray);

    m_ChunkRenderer->Render();

    // --- Draw ImGui UI ---
    float xscale, yscale;
    glfwGetWindowContentScale(m_Window, &xscale, &yscale);
    ImGui::GetIO().FontGlobalScale = xscale;

    GUIRenderer::RenderStatsOverview(m_ChunkRenderer->GetTotalVertexCount());
    if (!m_InCamera) GUIRenderer::RenderSettingsScreen(m_Settings, m_Camera, m_ChunkRenderer, m_World);
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