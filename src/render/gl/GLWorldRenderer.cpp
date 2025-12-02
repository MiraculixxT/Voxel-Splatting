#include "render/gl/GLWorldRenderer.hpp"

#include "Application.hpp"
#include "render/gl/GLTextureUtils.hpp"


GLWorldRenderer::~GLWorldRenderer() {
    delete m_BlockShader;
    delete m_ChunkRenderer;
    glDeleteTextures(1, &m_TextureArray);
}

void GLWorldRenderer::Init() {
    // Setup shader
    m_BlockShader = new GLShader("assets/shaders/block.vsh", "assets/shaders/block.fsh");

    // Define the texture order. This MUST match BlockData::Init()
    const std::vector<std::string> textureFiles = {
        "assets/textures/stone.png",       // i 0
        "assets/textures/dirt.png",        // i 1
        "assets/textures/grass_top.png",   // i 2
        "assets/textures/grass_side.png",   // i 3
        "assets/textures/sand.png",   // i 4
        "assets/textures/snow.png",   // i 5
        "assets/textures/water.png"   // i 6
    };
    m_TextureArray = GLTextureUtils::LoadTexture2DArray(textureFiles);

    m_BlockShader->use();
    m_BlockShader->setInt("textureArray", 0);

    // --- 8. Create World/Chunk ---
    m_ChunkRenderer = new GLChunkRenderer(m_Camera, m_Settings);
    for (auto [cx, column] : m_World.getChunks()) {
        for (auto& [cy, chunk] : column) {
            chunk->BuildMesh(m_World);
            m_ChunkRenderer->UploadMesh(cx, cy, chunk->GetMeshVertices());
        }
    }

}

void GLWorldRenderer::RenderWorld() { // performs sub function edits, so const is not possible
    // --- Clear Screen ---
    glClearColor(0.5f, 0.8f, 1.0f, 1.0f); // Sky blue
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // --- Draw World ---
    m_BlockShader->use();
    const glm::mat4 projection = glm::perspective(glm::radians(m_Camera.Zoom), static_cast<float>(SCR_WIDTH) / static_cast<float>(SCR_HEIGHT), m_Settings.GLFrom, m_Settings.GLTo);
    const auto view = m_Camera.GetViewMatrix();
    constexpr auto model = glm::mat4(1.0f);

    m_BlockShader->setMat4("projection", projection);
    m_BlockShader->setMat4("view", view);
    m_BlockShader->setMat4("model", model);

    m_BlockShader->setVec3("cameraPosition", m_Camera.Position);
    m_BlockShader->setFloat("fogStart", m_Settings.GLTo * m_Settings.FogStartMult);
    m_BlockShader->setFloat("fogEnd", m_Settings.GLTo * m_Settings.FogEndMult);
    m_BlockShader->setFloat("time", static_cast<float>(glfwGetTime()));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_TextureArray);

    // Check from where to where we should render
    ViewFrustum frustum;
    frustum.Update(projection * view);
    const glm::ivec2 center(std::floor(m_Camera.Position.x), std::floor(m_Camera.Position.z));
    const int renderDistance = std::floor(m_Settings.GLTo);
    const int fromX = (center.x - renderDistance) / CHUNK_WIDTH - 1;
    const int toX   = (center.x + renderDistance) / CHUNK_WIDTH;
    const int fromZ = (center.y - renderDistance) / CHUNK_WIDTH - 1;
    const int toZ   = (center.y + renderDistance) / CHUNK_WIDTH;
    m_ChunkRenderer->Render(frustum, fromX, toX, fromZ, toZ);
}

