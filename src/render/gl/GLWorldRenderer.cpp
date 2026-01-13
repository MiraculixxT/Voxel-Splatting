#include "render/gl/GLWorldRenderer.hpp"

#include "Application.hpp"
#include "render/gl/GLTextureUtils.hpp"

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

#include "iostream"
#include <vector>
#include <string>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <map>
#include <utility>

#include "game/Chunk.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace {
    struct GroundTruthCapture {
        GLuint fbo = 0;
        GLuint colorTex = 0;
        GLuint depthRbo = 0;
        int w = 0;
        int h = 0;
        int frameIndex = 0;

        void init(int width, int height) {
            w = width;
            h = height;

            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);

            glGenTextures(1, &colorTex);
            glBindTexture(GL_TEXTURE_2D, colorTex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glBindTexture(GL_TEXTURE_2D, 0);

            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);

            glGenRenderbuffers(1, &depthRbo);
            glBindRenderbuffer(GL_RENDERBUFFER, depthRbo);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
            glBindRenderbuffer(GL_RENDERBUFFER, 0);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthRbo);

            GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (status != GL_FRAMEBUFFER_COMPLETE) {
                std::cerr << "Failed to create GT capture FBO" << std::endl;
            }

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        void destroy() {
            if (depthRbo) glDeleteRenderbuffers(1, &depthRbo);
            if (colorTex) glDeleteTextures(1, &colorTex);
            if (fbo) glDeleteFramebuffers(1, &fbo);
            depthRbo = 0;
            colorTex = 0;
            fbo = 0;
        }

        void captureToPNG(const std::string& folder) {
            if (!fbo || w <= 0 || h <= 0) return;

            std::filesystem::create_directories(folder);

            std::vector<unsigned char> pixels(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

            // Flip vertically (OpenGL origin is bottom-left)
            for (int y = 0; y < h / 2; ++y) {
                unsigned char* row = pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(w) * 4;
                unsigned char* opp = pixels.data() + static_cast<size_t>(h - 1 - y) * static_cast<size_t>(w) * 4;
                for (int x = 0; x < w * 4; ++x) {
                    std::swap(row[x], opp[x]);
                }
            }

            char name[256];
            std::snprintf(name, sizeof(name), "%s/gt_%05d.png", folder.c_str(), frameIndex++);
            stbi_write_png(name, w, h, 4, pixels.data(), w * 4);
            std::cout << "Saved GT: " << name << std::endl;
        }

        static float linearizeDepth(float depth01, float nearZ, float farZ) {
            // OpenGL depth buffer is non-linear in [0,1]. Convert back to view-space Z (positive forward).
            // Assumes standard perspective projection with depth range [0,1].
            const float z = depth01 * 2.0f - 1.0f; // back to NDC [-1,1]
            return (2.0f * nearZ * farZ) / (farZ + nearZ - z * (farZ - nearZ));
        }

        void captureDepthLinearToBIN(const std::string& folder, float nearZ, float farZ) {
            if (!fbo || w <= 0 || h <= 0) return;
            std::filesystem::create_directories(folder);

            std::vector<float> depth(static_cast<size_t>(w) * static_cast<size_t>(h));
            glReadPixels(0, 0, w, h, GL_DEPTH_COMPONENT, GL_FLOAT, depth.data());

            // Flip vertically to match the PNG
            for (int y = 0; y < h / 2; ++y) {
                float* row = depth.data() + static_cast<size_t>(y) * static_cast<size_t>(w);
                float* opp = depth.data() + static_cast<size_t>(h - 1 - y) * static_cast<size_t>(w);
                for (int x = 0; x < w; ++x) {
                    std::swap(row[x], opp[x]);
                }
            }

            // Linearize
            for (size_t i = 0; i < depth.size(); ++i) {
                depth[i] = linearizeDepth(depth[i], nearZ, farZ);
            }

            char name[256];
            std::snprintf(name, sizeof(name), "%s/gt_%05d.depth.bin", folder.c_str(), frameIndex - 1);
            std::ofstream out(name, std::ios::binary);
            out.write(reinterpret_cast<const char*>(depth.data()), static_cast<std::streamsize>(depth.size() * sizeof(float)));
            out.close();
        }

        void captureCameraMeta(const std::string& folder, const glm::mat4& view, const glm::mat4& proj) {
            if (!fbo || w <= 0 || h <= 0) return;
            std::filesystem::create_directories(folder);

            char name[256];
            std::snprintf(name, sizeof(name), "%s/gt_%05d.meta.txt", folder.c_str(), frameIndex - 1);
            std::ofstream out(name);
            out << "w " << w << "\n";
            out << "h " << h << "\n";
            out << "view ";
            for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) out << view[c][r] << (r == 3 && c == 3 ? "\n" : " ");
            out << "proj ";
            for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) out << proj[c][r] << (r == 3 && c == 3 ? "\n" : " ");
            out.close();
        }
    };

    GroundTruthCapture g_gt;

    // Global toggle used by the F7 hotkey
    static bool g_useTrainedSplats = false;

    // Cache trained splats loaded from a folder (so we don't re-read every toggle)
    template <typename TSplat>
    struct TrainedCache {
        bool loaded = false;
        std::string folderAbs;
        std::map<std::pair<int,int>, std::vector<TSplat>> byChunk;
    };

    template <typename TSplat>
    static TrainedCache<TSplat>& GetTrainedCache() {
        static TrainedCache<TSplat> cache;
        return cache;
    }

    template <typename TSplat>
    static void LoadTrainedFolderOnce(const std::string& trainedFolder) {
        auto& cache = GetTrainedCache<TSplat>();

        const std::string abs = std::filesystem::absolute(trainedFolder).string();
        if (cache.loaded && cache.folderAbs == abs) return;

        cache.loaded = true;
        cache.folderAbs = abs;
        cache.byChunk.clear();

        if (!std::filesystem::exists(cache.folderAbs)) {
            std::cout << "Trained folder does not exist: '" << cache.folderAbs << "'" << std::endl;
            return;
        }

        size_t files = 0;
        for (const auto& entry : std::filesystem::directory_iterator(cache.folderAbs)) {
            if (!entry.is_regular_file()) continue;
            const auto p = entry.path();
            if (p.extension() != ".bin") continue;
            // Accept any name; we read cx/cy from the header
            int cx = 0, cy = 0;
            std::vector<TSplat> spl;
            if (ReadSPL2v2ChunkFile<TSplat>(p.string(), cx, cy, spl)) {
                cache.byChunk[{cx, cy}] = std::move(spl);
                files++;
            }
        }

        std::cout << "Loaded trained cache: filesRead=" << files
                  << " chunksCached=" << cache.byChunk.size()
                  << " folder='" << cache.folderAbs << "'" << std::endl;
    }

    template <typename TSplat>
    static bool ReadSPL2v2ChunkFile(const std::string& path, int& outCx, int& outCy, std::vector<TSplat>& outSplats) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;

        // Header: magic(u32), version(u32), cx(i32), cy(i32), count(u32), stride(u32)
        uint32_t magic = 0, version = 0, count = 0, stride = 0;
        int32_t cx = 0, cy = 0;
        in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        in.read(reinterpret_cast<char*>(&version), sizeof(version));
        in.read(reinterpret_cast<char*>(&cx), sizeof(cx));
        in.read(reinterpret_cast<char*>(&cy), sizeof(cy));
        in.read(reinterpret_cast<char*>(&count), sizeof(count));
        in.read(reinterpret_cast<char*>(&stride), sizeof(stride));

        if (!in) return false;
        if (magic != 0x53504C32u || version != 2u) {
            std::cerr << "Trained splat file has wrong header (expected SPL2/v2): " << path << std::endl;
            return false;
        }
        // We expect 10 float32 per splat: pos3, scale3, color3, opacity1
        if (stride != 10u * sizeof(float)) {
            std::cerr << "Trained splat file has unexpected stride " << stride << " (expected " << (10u * sizeof(float)) << ") in " << path << std::endl;
            return false;
        }

        outCx = static_cast<int>(cx);
        outCy = static_cast<int>(cy);
        outSplats.clear();
        outSplats.reserve(static_cast<size_t>(count));

        for (uint32_t i = 0; i < count; ++i) {
            float data[10];
            in.read(reinterpret_cast<char*>(data), sizeof(data));
            if (!in) break;

            TSplat s{};
            s.position = glm::vec3(data[0], data[1], data[2]);
            s.scale    = glm::vec3(data[3], data[4], data[5]);
            s.color    = glm::vec3(data[6], data[7], data[8]);
            s.opacity  = data[9];

            // Defaults for fields not stored in SPL2/v2
            s.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            s.uv     = glm::vec2(0.0f, 0.0f);
            s.layer  = 0.0f;

            outSplats.push_back(s);
        }

        if (outSplats.size() != static_cast<size_t>(count)) {
            std::cerr << "Trained splat file ended early: " << path << " read " << outSplats.size() << " / " << count << std::endl;
        }

        return !outSplats.empty();
    }

    template <typename TSplat>
    static void ApplySplatOverrideToLoadedWorld(const std::string& trainedFolder,
                                                GLSplatRenderer* splatRenderer,
                                                const decltype(std::declval<World>().getChunks())& chunks) {
        if (!splatRenderer) return;

        // Ensure cache is loaded (reads cx/cy from headers, independent of filenames)
        LoadTrainedFolderOnce<TSplat>(trainedFolder);
        auto& cache = GetTrainedCache<TSplat>();

        size_t trainedUsed = 0;
        size_t totalChunks = 0;

        for (auto [cx, column] : chunks) {
            for (auto& [cy, chunk] : column) {
                totalChunks++;

                // If override is OFF, always upload original splats
                if (!g_useTrainedSplats) {
                    splatRenderer->UploadSplats(cx, cy, chunk->GetSplats());
                    continue;
                }

                // Override ON: use trained if we have it, else fallback
                auto it = cache.byChunk.find({cx, cy});
                if (it != cache.byChunk.end()) {
                    splatRenderer->UploadSplats(cx, cy, it->second);
                    trainedUsed++;
                } else {
                    splatRenderer->UploadSplats(cx, cy, chunk->GetSplats());
                }
            }
        }

        std::cout << "Splat override applied. trainedUsed=" << trainedUsed << " / " << totalChunks
                  << " folder='" << cache.folderAbs << "'" << std::endl;
    }


    template <typename TSplat>
    static void DumpChunkSplatsBinary(const std::string& folder, int cx, int cy, const std::vector<TSplat>& splats) {
        std::filesystem::create_directories(folder);

        // File name: chunk_<cx>_<cy>.splats.bin
        std::ostringstream oss;
        oss << folder << "/chunk_" << cx << "_" << cy << ".splats.bin";
        const std::string path = oss.str();

        std::ofstream out(path, std::ios::binary);
        if (!out) {
            std::cerr << "Failed to write splat dump: " << path << std::endl;
            return;
        }

        // Header: magic + version + cx/cy + count + stride
        const uint32_t magic = 0x53504C32; // 'SPL2'
        const uint32_t version = 2;
        const int32_t icx = static_cast<int32_t>(cx);
        const int32_t icy = static_cast<int32_t>(cy);
        const uint32_t count = static_cast<uint32_t>(splats.size());
        // Portable layout: 10 float32 per splat (pos3, scale3, color3, opacity1)
        const uint32_t stride = static_cast<uint32_t>(10 * sizeof(float));

        out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
        out.write(reinterpret_cast<const char*>(&version), sizeof(version));
        out.write(reinterpret_cast<const char*>(&icx), sizeof(icx));
        out.write(reinterpret_cast<const char*>(&icy), sizeof(icy));
        out.write(reinterpret_cast<const char*>(&count), sizeof(count));
        out.write(reinterpret_cast<const char*>(&stride), sizeof(stride));

        if (!splats.empty()) {
            for (const auto& s : splats) {
                const float data[10] = {
                    s.position.x, s.position.y, s.position.z,
                    s.scale.x,    s.scale.y,    s.scale.z,
                    s.color.r,    s.color.g,    s.color.b,
                    s.opacity
                };
                out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(sizeof(data)));
            }
        }

        out.close();
    }

    static void DumpAllLoadedSplats(const std::string& folder, const decltype(std::declval<World>().getChunks())& chunks) {
        std::filesystem::create_directories(folder);

        std::ofstream manifest(folder + "/manifest.txt");
        if (!manifest) {
            std::cerr << "Failed to write splat manifest" << std::endl;
            return;
        }

        size_t totalSplats = 0;
        size_t totalChunks = 0;

        for (auto [cx, column] : chunks) {
            for (auto& [cy, chunk] : column) {
                const auto& splats = chunk->GetSplats();
                DumpChunkSplatsBinary(folder, cx, cy, splats);
                manifest << "chunk " << cx << " " << cy << " splats " << splats.size() << "\n";
                totalSplats += splats.size();
                totalChunks++;
            }
        }

        manifest << "total_chunks " << totalChunks << "\n";
        manifest << "total_splats " << totalSplats << "\n";
        manifest.close();

        std::cout << "Dumped splats: " << totalSplats << " across " << totalChunks << " chunks to " << folder << std::endl;
    }
}


GLWorldRenderer::~GLWorldRenderer() {
    g_gt.destroy();
    delete m_BlockShader;
    delete m_ChunkRenderer;
    delete m_SplatRenderer;
    delete m_ShadowShader;
    delete m_SkyShader;
    delete m_SunFlareShader;
    delete m_GodrayShader;
    delete m_GodrayOcclusionShader;
    glDeleteTextures(1, &m_TextureArray);
    glDeleteVertexArrays(1, &m_SkyVAO);
    glDeleteFramebuffers(1, &m_GodrayOcclusionFBO);
    glDeleteTextures(1, &m_GodrayOcclusionTex);
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

    m_GodrayShader = new GLShader(
            "assets/shaders/godrays.vsh",
            "assets/shaders/godrays.fsh"
    );

    m_GodrayOcclusionShader = new GLShader(
            "assets/shaders/godrays_occlusion.vsh",
            "assets/shaders/godrays_occlusion.fsh"
    );

    // Create low-resolution occlusion texture and FBO for god rays
    {
        int occWidth  = SCR_WIDTH  / 2;
        int occHeight = SCR_HEIGHT / 2;

        glGenTextures(1, &m_GodrayOcclusionTex);
        glBindTexture(GL_TEXTURE_2D, m_GodrayOcclusionTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, occWidth, occHeight, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);

        glGenFramebuffers(1, &m_GodrayOcclusionFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, m_GodrayOcclusionFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_GodrayOcclusionTex, 0);

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "Failed to create godray occlusion FBO" << std::endl;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

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

    // Ground-truth capture target (reference images)
    // NOTE: keep GT capture optional; it is not needed for runtime rendering.
    g_gt.init(SCR_WIDTH, SCR_HEIGHT);

    // --- 8. Create World/Chunk ---
    m_ChunkRenderer = new GLChunkRenderer(m_Camera, m_Settings);
    m_SplatRenderer = new GLSplatRenderer();

    // Build and upload initial meshes for already generated chunks
    for (auto [cx, column] : m_World.getChunks()) {
        for (auto& [cy, chunk] : column) {
            chunk->BuildMesh(m_World);
            m_ChunkRenderer->UploadMesh(cx, cy, chunk->GetMeshVertices());
        }
    }

    // Load trained splats from disk (produced by the training pipeline)
    // Expected files: captures/splats/chunk_<cx>_<cz>.splats.bin
    if (m_SplatRenderer) {
        const bool ok = m_SplatRenderer->LoadTrainedChunkFolder("captures/splats");
        std::cout << (ok ? "[Splats] Trained chunk folder loaded.\n" : "[Splats] No trained splats loaded from captures/splats (folder missing/empty?).\n");
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

    // --- Debug / capture hotkeys ---
    GLFWwindow* win = glfwGetCurrentContext();

    // F8: dump current loaded splats (generated in-engine) to disk
    {
        static bool prevF8 = false;
        const bool f8 = (win && glfwGetKey(win, GLFW_KEY_F8) == GLFW_PRESS);
        const bool f8Edge = f8 && !prevF8;
        prevF8 = f8;

        if (f8Edge) {
            DumpAllLoadedSplats("captures/splats_v2", m_World.getChunks());
        }
    }

    // F7: toggle trained-splats override and re-upload splats for all loaded chunks
    {
        static bool prevF7 = false;
        const bool f7 = (win && glfwGetKey(win, GLFW_KEY_F7) == GLFW_PRESS);
        const bool f7Edge = f7 && !prevF7;
        prevF7 = f7;

        if (f7Edge) {
            g_useTrainedSplats = !g_useTrainedSplats;
            std::cout << (g_useTrainedSplats ? "Trained splats: ON" : "Trained splats: OFF") << std::endl;

            using TSplat = Splat;
            ApplySplatOverrideToLoadedWorld<TSplat>("captures/splats_trained", m_SplatRenderer, m_World.getChunks());
        }
    }

    // F9: capture once | F10: toggle continuous capture (every N frames)
    {
        static bool prevF9 = false;
        static bool prevF10 = false;
        static bool continuous = false;
        static int frameCounter = 0;
        constexpr int CAPTURE_EVERY_N_FRAMES = 30;

        const bool f9 = (win && glfwGetKey(win, GLFW_KEY_F9) == GLFW_PRESS);
        const bool f10 = (win && glfwGetKey(win, GLFW_KEY_F10) == GLFW_PRESS);

        const bool f9Edge = f9 && !prevF9;
        const bool f10Edge = f10 && !prevF10;

        prevF9 = f9;
        prevF10 = f10;

        if (f10Edge) {
            continuous = !continuous;
            std::cout << (continuous ? "GT continuous capture: ON" : "GT continuous capture: OFF") << std::endl;
            frameCounter = 0;
        }

        bool doCapture = false;
        if (f9Edge) {
            doCapture = true;
        } else if (continuous) {
            doCapture = (frameCounter++ % CAPTURE_EVERY_N_FRAMES) == 0;
        }

        if (doCapture) {
            // Render ONLY the mesh world (chunks) into the GT FBO and save RGB+Depth+Camera.
            GLint prevFBO = 0;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);

            GLint prevViewport[4];
            glGetIntegerv(GL_VIEWPORT, prevViewport);

            glBindFramebuffer(GL_FRAMEBUFFER, g_gt.fbo);
            glViewport(0, 0, g_gt.w, g_gt.h);
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);

            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            const float aspectGT = static_cast<float>(g_gt.w) / static_cast<float>(g_gt.h);
            const glm::mat4 projectionGT = glm::perspective(
                glm::radians(m_Camera.Zoom),
                aspectGT,
                m_Settings.GLFrom,
                m_Settings.GLTo
            );
            const glm::mat4 viewGT = m_Camera.GetViewMatrix();

            m_BlockShader->use();
            m_BlockShader->setMat4("projection", projectionGT);
            m_BlockShader->setMat4("view", viewGT);
            constexpr auto modelGT = glm::mat4(1.0f);
            m_BlockShader->setMat4("model", modelGT);
            m_BlockShader->setMat4("lightViewProj", m_LightViewProj);
            m_BlockShader->setVec3("uLightDir", m_SunDir);
            m_BlockShader->setVec3("cameraPosition", m_Camera.Position);

            // Disable fog for GT
            m_BlockShader->setFloat("fogStart", 1e9f);
            m_BlockShader->setFloat("fogEnd", 1e9f + 1.0f);

            // Freeze time for GT
            m_BlockShader->setFloat("time", 0.0f);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D_ARRAY, m_TextureArray);
            glActiveTexture(GL_TEXTURE1);
            m_ShadowMap.bindForRead(GL_TEXTURE1);
            m_BlockShader->setInt("uShadowMap", 1);

            ViewFrustum frustumGT;
            frustumGT.Update(projectionGT * viewGT);
            const glm::ivec2 centerGT(std::floor(m_Camera.Position.x), std::floor(m_Camera.Position.z));
            const int renderDistanceGT = std::floor(m_Settings.GLTo);
            const int fromXGT = (centerGT.x - renderDistanceGT) / CHUNK_WIDTH - 1;
            const int toXGT   = (centerGT.x + renderDistanceGT) / CHUNK_WIDTH;
            const int fromZGT = (centerGT.y - renderDistanceGT) / CHUNK_WIDTH - 1;
            const int toZGT   = (centerGT.y + renderDistanceGT) / CHUNK_WIDTH;

            m_ChunkRenderer->Render(frustumGT, fromXGT, toXGT, fromZGT, toZGT);

            g_gt.captureToPNG("captures/gt");
            g_gt.captureDepthLinearToBIN("captures/gt", m_Settings.GLFrom, m_Settings.GLTo);
            g_gt.captureCameraMeta("captures/gt", viewGT, projectionGT);

            glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
            glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
        }
    }

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

    // --- Godray occlusion pass: render geometry into low-res occlusion texture ---
    if (m_GodrayOcclusionShader) {
        // Save current framebuffer and viewport
        GLint prevFBO = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);

        GLint prevViewport[4];
        glGetIntegerv(GL_VIEWPORT, prevViewport);

        int occWidth  = SCR_WIDTH  / 2;
        int occHeight = SCR_HEIGHT / 2;

        glBindFramebuffer(GL_FRAMEBUFFER, m_GodrayOcclusionFBO);
        glViewport(0, 0, occWidth, occHeight);

        // Clear to 1.0 (sky passes through), occlusion shader draws geometry as 0.0
        glDisable(GL_DEPTH_TEST);
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        m_GodrayOcclusionShader->use();
        m_GodrayOcclusionShader->setMat4("projection", projection);
        m_GodrayOcclusionShader->setMat4("view", view);
        glm::mat4 occModel(1.0f);
        m_GodrayOcclusionShader->setMat4("model", occModel);
        // Bind block texture array so occlusion shader can sample alpha (for leaves etc.)
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, m_TextureArray);
        m_GodrayOcclusionShader->setInt("textureArray", 0);

        // Reuse same chunk range as normal rendering for occlusion
        ViewFrustum occFrustum;
        occFrustum.Update(projection * view);
        const glm::ivec2 occCenter(std::floor(m_Camera.Position.x), std::floor(m_Camera.Position.z));
        const int occRenderDistance = std::floor(m_Settings.GLTo);
        const int occFromX = (occCenter.x - occRenderDistance) / CHUNK_WIDTH - 1;
        const int occToX   = (occCenter.x + occRenderDistance) / CHUNK_WIDTH;
        const int occFromZ = (occCenter.y - occRenderDistance) / CHUNK_WIDTH - 1;
        const int occToZ   = (occCenter.y + occRenderDistance) / CHUNK_WIDTH;

        // Render all chunks into occlusion buffer using current shader
        m_ChunkRenderer->RenderAll(occFromX, occToX, occFromZ, occToZ);

        // Restore previous framebuffer and viewport
        glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
        glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
        glEnable(GL_DEPTH_TEST);
    }

    // --- Compute sun screen position and base intensity for post effects (flare, godrays) ---
    glm::vec2 sunScreen(0.5f, 0.5f);
    float sunIntensity = 0.0f;
    bool sunOnScreen = false;

    if (m_SunFlareShader || m_GodrayShader) {
        // Place the sun at a finite distance in front of the camera along -m_SunDir
        glm::vec3 sunDirToSun = -m_SunDir;
        float sunDistance = 100.0f; // must be within camera far plane (GLTo)
        glm::vec3 sunWorldPos = m_Camera.Position + sunDirToSun * sunDistance;

        // Project to clip space
        glm::vec4 sunClip = projection * view * glm::vec4(sunWorldPos, 1.0f);

        if (sunClip.w > 0.0f) {
            glm::vec3 sunNdc = glm::vec3(sunClip) / sunClip.w;

            sunScreen.x = sunNdc.x * 0.5f + 0.5f;
            sunScreen.y = sunNdc.y * 0.5f + 0.5f;

            // Only if sun is inside the screen bounds
            if (sunScreen.x >= 0.0f && sunScreen.x <= 1.0f &&
                sunScreen.y >= 0.0f && sunScreen.y <= 1.0f) {

                // Intensity based on distance to screen center (strongest in the middle)
                float centerDist = glm::length(sunScreen - glm::vec2(0.5f, 0.5f));
                sunIntensity = glm::clamp(1.0f - centerDist * 1.5f, 0.0f, 1.0f);

                sunOnScreen = (sunIntensity > 0.0f);
            }
        }
    }

    // --- Sun flare (lens glare) overlay ---
    if (m_SunFlareShader && sunOnScreen && sunIntensity > 0.0f) {
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        // Additive blending so the flare really adds light
        glBlendFunc(GL_ONE, GL_ONE);

        m_SunFlareShader->use();
        m_SunFlareShader->setVec2("uSunScreenPos", sunScreen);
        m_SunFlareShader->setFloat("uIntensity", sunIntensity);

        // Bind occlusion texture so flare can be hidden when sun is behind geometry
        if (m_GodrayOcclusionTex != 0) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_GodrayOcclusionTex);
            m_SunFlareShader->setInt("uOcclusionTex", 0);
        }

        glBindVertexArray(m_SkyVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);

        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
    }

    if (m_GodrayShader && sunOnScreen && sunIntensity > 0.0f) {
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE); // additiv

        m_GodrayShader->use();
        m_GodrayShader->setVec2("uSunScreenPos", sunScreen);
        m_GodrayShader->setFloat("uIntensity", sunIntensity);

        // Bind occlusion texture
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_GodrayOcclusionTex);
        m_GodrayShader->setInt("uOcclusionTex", 0);

        glBindVertexArray(m_SkyVAO); // Fullscreen-Triangle VAO wie beim Sun-Flare
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);

        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
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
