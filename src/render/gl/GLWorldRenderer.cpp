#include "render/gl/GLWorldRenderer.hpp"

#include "Application.hpp"
#include "render/gl/GLTextureUtils.hpp"

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include "iostream"


GLWorldRenderer::~GLWorldRenderer() {
    delete m_BlockShader;
    delete m_ChunkRenderer;
    delete m_SplatRenderer;
    delete m_ShadowShader;
    delete m_SkyShader;
    delete m_SunFlareShader;
    glDeleteTextures(1, &m_TextureArray);
    glDeleteVertexArrays(1, &m_SkyVAO);
}

void GLWorldRenderer::Init() {
    // Setup shader
    m_BlockShader = new GLShader("assets/shaders/block.vsh", "assets/shaders/block.fsh");

    // Setup shadow depth shader
    m_ShadowShader = new GLShader("assets/shaders/shadow_depth.vsh",
                                  "assets/shaders/shadow_depth.fsh");

    // Setup sky shader
    m_SkyShader = new GLShader("assets/shaders/sky.vsh", "assets/shaders/sky.fsh");

    // Setup sun flare shader (lens glare)
    m_SunFlareShader = new GLShader("assets/shaders/sun_flare.vsh", "assets/shaders/sun_flare.fsh");

    // Initialize shadow map (resolution can be tuned)
    if (!m_ShadowMap.init(2048, 2048)) {
        std::cerr << "Failed to init shadow map" << std::endl;
    }

    // Define the texture order. This MUST match BlockData::Init()
    const std::vector<std::string> textureFiles = {
        "assets/textures/stone.png",       // i 0
        "assets/textures/dirt.png",        // i 1
        "assets/textures/grass_top.png",   // i 2
        "assets/textures/grass_side.png",   // i 3
        "assets/textures/sand.png",   // i 4
        "assets/textures/snow.png",   // i 5
        "assets/textures/water.png",   // i 6
        "assets/textures/log_side.png",   // i 7
        "assets/textures/log_top.png",   // i 8
        "assets/textures/leave.png"   // i 9
    };
    m_TextureArray = GLTextureUtils::LoadTexture2DArray(textureFiles);

    m_BlockShader->use();
    m_BlockShader->setInt("textureArray", 0);

    // Initialize sun direction
    m_SunDir = glm::normalize(glm::vec3(-0.3f, -1.0f, -0.2f));

    // Create VAO for fullscreen sky triangle
    glGenVertexArrays(1, &m_SkyVAO);

    // --- 8. Create World/Chunk ---
    m_ChunkRenderer = new GLChunkRenderer(m_Camera, m_Settings);
    m_SplatRenderer = new GLSplatRenderer();

    // Register renderers in world so the mesh worker can upload to them

    // Build and upload initial meshes and splats for already generated chunks
    for (auto [cx, column] : m_World.getChunks()) {
        for (auto& [cy, chunk] : column) {
            chunk->BuildMesh(m_World);
            chunk->BuildSplats(m_World);
            m_ChunkRenderer->UploadMesh(cx, cy, chunk->GetMeshVertices());
            m_SplatRenderer->UploadSplats(cx, cy, chunk->GetSplats());
        }
    }
}

void GLWorldRenderer::RenderWorld() { // performs sub function edits, so const is not possible
    // --- Shadow Mapping: compute directional light matrix ---
    // Light direction (directional sun light)
    glm::vec3 lightDir = m_SunDir;

    // Focus point for shadow area (camera-centered cascaded-like approach)
    glm::vec3 lightCenter = m_Camera.Position;

    // Light position far along the opposite direction
    glm::vec3 lightPos = lightCenter - lightDir * 100.0f;

    // View matrix from the light's perspective
    glm::mat4 lightView = glm::lookAt(lightPos, lightCenter, glm::vec3(0, 1, 0));

    // Orthographic projection for stable directional shadows
    float orthoSize = 80.0f;
    glm::mat4 lightProj = glm::ortho(-orthoSize, orthoSize,
                                     -orthoSize, orthoSize,
                                      1.0f, 300.0f);

    // Combined light-space view-projection matrix
    m_LightViewProj = lightProj * lightView;

    // 1) Render depth into shadow map
    RenderShadowPass();

    // 2) Clear main framebuffer and render normal view
    glClearColor(0.5f, 0.8f, 1.0f, 1.0f); // background color is mostly irrelevant, sky will overwrite
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);


    // Compute projection and view matrices once
    const glm::mat4 projection = glm::perspective(
        glm::radians(m_Camera.Zoom),
        static_cast<float>(SCR_WIDTH) / static_cast<float>(SCR_HEIGHT),
        m_Settings.GLFrom,
        m_Settings.GLTo
    );
    const glm::mat4 view = m_Camera.GetViewMatrix();

    // --- Draw Sky ---
    if (m_SkyShader) {
        glm::mat4 viewNoTranslation = glm::mat4(glm::mat3(view));
        glm::mat4 invViewProj = glm::inverse(projection * viewNoTranslation);

        glDepthMask(GL_FALSE);
        glDisable(GL_DEPTH_TEST);

        m_SkyShader->use();
        m_SkyShader->setMat4("uInvViewProj", invViewProj);
        m_SkyShader->setVec3("uCameraPos", m_Camera.Position);
        m_SkyShader->setVec3("uSunDir", m_SunDir);

        glBindVertexArray(m_SkyVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);

        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
    }

    // --- Draw World ---
    m_BlockShader->use();
    m_BlockShader->setMat4("projection", projection);
    m_BlockShader->setMat4("view", view);
    constexpr auto model = glm::mat4(1.0f);
    m_BlockShader->setMat4("model", model);
    // Provide light-space matrix to block shader
    m_BlockShader->setMat4("lightViewProj", m_LightViewProj);
    m_BlockShader->setVec3("uLightDir", lightDir);

    m_BlockShader->setVec3("cameraPosition", m_Camera.Position);
    m_BlockShader->setFloat("fogStart", m_Settings.GLTo * m_Settings.FogStartMult);
    m_BlockShader->setFloat("fogEnd", m_Settings.GLTo * m_Settings.FogEndMult);
    m_BlockShader->setFloat("time", static_cast<float>(glfwGetTime()));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_TextureArray);
    // Bind shadow map to texture unit 1
    glActiveTexture(GL_TEXTURE1);
    m_ShadowMap.bindForRead(GL_TEXTURE1);
    m_BlockShader->setInt("uShadowMap", 1);

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

    // Render Gaussian splats using the same view-projection and lighting/shadow data
    if (m_SplatRenderer) {
        const glm::mat4 viewProj = projection * view;

        // configure lighting/shadows for splats (reuse same light matrix and shadow map)
        m_SplatRenderer->SetLighting(m_LightViewProj, m_ShadowMap.getDepthTexture(), m_SunDir);

        // camera chunk coordinates
        const int camChunkX = static_cast<int>(std::floor(m_Camera.Position.x)) / CHUNK_WIDTH;
        const int camChunkZ = static_cast<int>(std::floor(m_Camera.Position.z)) / CHUNK_WIDTH;

        // how many chunks around the player to draw splats
        const int SPLAT_RENDER_DISTANCE_CHUNKS = 2;

        m_SplatRenderer->Draw(viewProj, camChunkX, camChunkZ, SPLAT_RENDER_DISTANCE_CHUNKS);
    }

    // --- Sun flare (lens glare) overlay ---
    if (m_SunFlareShader) {
        // Place the sun at a finite distance in front of the camera along -m_SunDir
        glm::vec3 sunDirToSun = -m_SunDir;
        float sunDistance = 100.0f; // must be within camera far plane (GLTo)
        glm::vec3 sunWorldPos = m_Camera.Position + sunDirToSun * sunDistance;

        // Project to clip space
        glm::vec4 sunClip = projection * view * glm::vec4(sunWorldPos, 1.0f);

        if (sunClip.w > 0.0f) {
            glm::vec3 sunNdc = glm::vec3(sunClip) / sunClip.w;

            glm::vec2 sunScreen;
            sunScreen.x = sunNdc.x * 0.5f + 0.5f;
            sunScreen.y = sunNdc.y * 0.5f + 0.5f;

            // Only if sun is inside the screen bounds
            if (sunScreen.x >= 0.0f && sunScreen.x <= 1.0f &&
                sunScreen.y >= 0.0f && sunScreen.y <= 1.0f) {

                // Intensity based on distance to screen center (strongest in the middle)
                float centerDist = glm::length(sunScreen - glm::vec2(0.5f, 0.5f));
                float intensity = glm::clamp(1.0f - centerDist * 1.5f, 0.0f, 1.0f);

                if (intensity > 0.0f) {
                    glDisable(GL_DEPTH_TEST);
                    glEnable(GL_BLEND);
                    // Additive blending so the flare really adds light
                    glBlendFunc(GL_ONE, GL_ONE);

                    m_SunFlareShader->use();
                    m_SunFlareShader->setVec2("uSunScreenPos", sunScreen);
                    m_SunFlareShader->setFloat("uIntensity", intensity);

                    glBindVertexArray(m_SkyVAO);
                    glDrawArrays(GL_TRIANGLES, 0, 3);
                    glBindVertexArray(0);

                    glDisable(GL_BLEND);
                    glEnable(GL_DEPTH_TEST);
                }
            }
        }
    }
}


void GLWorldRenderer::RenderShadowPass() {
    // Save current viewport so we can restore it after rendering into the shadow map.
    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    m_ShadowMap.bindForWrite();
    glCullFace(GL_FRONT); // reduce peter-panning

    m_ShadowShader->use();
    m_ShadowShader->setMat4("uLightViewProj", m_LightViewProj);
    glm::mat4 identity(1.0f);
    m_ShadowShader->setMat4("uModel", identity);

    // Reuse same chunk range as normal rendering, but cull using LIGHT frustum
    ViewFrustum lightFrustum;
    lightFrustum.Update(m_LightViewProj);

    const glm::ivec2 center(std::floor(m_Camera.Position.x), std::floor(m_Camera.Position.z));
    const int renderDistance = std::floor(m_Settings.GLTo);
    const int fromX = (center.x - renderDistance) / CHUNK_WIDTH - 1;
    const int toX   = (center.x + renderDistance) / CHUNK_WIDTH;
    const int fromZ = (center.y - renderDistance) / CHUNK_WIDTH - 1;
    const int toZ   = (center.y + renderDistance) / CHUNK_WIDTH;

    // Render chunks into shadow map using light frustum
    m_ChunkRenderer->RenderAll(fromX, toX, fromZ, toZ);

    glCullFace(GL_BACK);
    // Restore previous viewport so main rendering uses the correct size.
    glViewport(prevViewport[0], prevViewport[1],
               prevViewport[2], prevViewport[3]);
    m_ShadowMap.unbind();
}
