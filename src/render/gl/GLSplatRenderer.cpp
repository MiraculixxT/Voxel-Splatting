#include "render/gl/GLSplatRenderer.hpp"

#include <iostream>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdint>
#include <vector>
#include <algorithm>

struct GPUInstance {
    glm::vec3 position;
    glm::vec3 scale;
    glm::vec3 normal;
    glm::vec3 color;
    float     opacity;
    glm::vec2 uv;
    float     layer;
};

static std::string loadShaderSource(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open shader file: " << path << std::endl;
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

GLSplatRenderer::GLSplatRenderer() {
    shaderProgram = createShaderProgram();
    initQuad();
}

GLSplatRenderer::~GLSplatRenderer() {
    ClearAll();

    if (shaderProgram) {
        glDeleteProgram(shaderProgram);
        shaderProgram = 0;
    }
}

void GLSplatRenderer::ClearAll() {
    for (auto& entry : buffers) {
        ChunkSplatBuffer& buf = entry.second;
        if (buf.vbo) {
            glDeleteBuffers(1, &buf.vbo);
            buf.vbo = 0;
        }
        if (buf.vao) {
            glDeleteVertexArrays(1, &buf.vao);
            buf.vao = 0;
        }
        buf.count = 0;
        buf.trained = false;
    }
    buffers.clear();

    // Note: m_QuadVbo is shared across all chunk VAOs; keep it alive for reuse.
}

bool GLSplatRenderer::readHeader(std::ifstream& in, SplatBinHeader& h) {
    // Read the first field; if this fails, we treat it as EOF.
    if (!in.read(reinterpret_cast<char*>(&h.magic), sizeof(h.magic))) {
        return false;
    }
    if (!in.read(reinterpret_cast<char*>(&h.version), sizeof(h.version))) return false;
    if (!in.read(reinterpret_cast<char*>(&h.cx), sizeof(h.cx))) return false;
    if (!in.read(reinterpret_cast<char*>(&h.cz), sizeof(h.cz))) return false;
    if (!in.read(reinterpret_cast<char*>(&h.count), sizeof(h.count))) return false;
    if (!in.read(reinterpret_cast<char*>(&h.stride), sizeof(h.stride))) return false;
    return true;
}

void GLSplatRenderer::decodeToSplats(const SplatBinHeader& h,
                                     const std::vector<uint8_t>& raw,
                                     std::vector<Splat>& out) {
    out.clear();
    if (h.count == 0) return;

    // We support stride >= 40 bytes. We only use the first 10 floats:
    // pos3, scale3, color3, opacity1.
    if (h.stride < 10 * sizeof(float)) {
        return;
    }

    const size_t expectedBytes = static_cast<size_t>(h.count) * static_cast<size_t>(h.stride);
    if (raw.size() < expectedBytes) {
        return;
    }

    out.reserve(h.count);

    for (uint32_t i = 0; i < h.count; ++i) {
        const uint8_t* base = raw.data() + static_cast<size_t>(i) * static_cast<size_t>(h.stride);
        const float* f = reinterpret_cast<const float*>(base);

        Splat s;
        s.position = glm::vec3(f[0], f[1], f[2]);
        s.scale    = glm::vec3(f[3], f[4], f[5]);
        s.color    = glm::vec3(f[6], f[7], f[8]);
        s.opacity  = f[9];

        // Trained dumps do not provide these in our simple portable format.
        // Provide sensible defaults so the existing shader path works.
        s.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        s.uv     = glm::vec2(0.0f);
        s.layer  = 0.0f;

        out.push_back(s);
    }
}

void GLSplatRenderer::UploadSplats(int cx, int cz, const std::vector<Splat>& splats)
{
    auto key = std::make_pair(cx, cz);
    ChunkSplatBuffer& buf = buffers[key];

    // Create VAO once
    if (buf.vao == 0) {
        glGenVertexArrays(1, &buf.vao);
    }

    glBindVertexArray(buf.vao);

    // 1) Bind quad VBO to VAO (attribute location 0)
    glBindBuffer(GL_ARRAY_BUFFER, m_QuadVbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
            0,              // location 0 in VS: inCorner
            2,              // vec2
            GL_FLOAT,
            GL_FALSE,
            2 * sizeof(float),
            (void*)0
    );
    glVertexAttribDivisor(0, 0); // per vertex, not per instance

    // 2) Create/update instance VBO (attributes 1..7)
    if (buf.vbo == 0) {
        glGenBuffers(1, &buf.vbo);
    }
    glBindBuffer(GL_ARRAY_BUFFER, buf.vbo);

    std::vector<GPUInstance> instances;
    instances.reserve(splats.size());
    for (const Splat& s : splats) {
        GPUInstance inst;
        inst.position = s.position;
        inst.scale    = s.scale;
        inst.normal   = s.normal;
        inst.color    = s.color;
        inst.opacity  = s.opacity;
        inst.uv       = s.uv;
        inst.layer    = s.layer;
        instances.push_back(inst);
    }

    buf.count = static_cast<GLsizei>(instances.size());

    if (!instances.empty()) {
        glBufferData(GL_ARRAY_BUFFER,
                     instances.size() * sizeof(GPUInstance),
                     instances.data(),
                     GL_STATIC_DRAW);
    } else {
        // mark empty chunk
        glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_STATIC_DRAW);
    }

    // Attribute 1: position (vec3)
    std::size_t offset = 0;
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
            1, 3, GL_FLOAT, GL_FALSE, sizeof(GPUInstance), (void*)offset
    );
    glVertexAttribDivisor(1, 1); // per instance

    // Attribute 2: scale (vec3)
    offset += sizeof(glm::vec3);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(
            2, 3, GL_FLOAT, GL_FALSE, sizeof(GPUInstance), (void*)offset
    );
    glVertexAttribDivisor(2, 1); // per instance

    // Attribute 3: normal (vec3)
    offset += sizeof(glm::vec3);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(
            3, 3, GL_FLOAT, GL_FALSE, sizeof(GPUInstance), (void*)offset
    );
    glVertexAttribDivisor(3, 1); // per instance

    // Attribute 4: color (vec3)
    offset += sizeof(glm::vec3);
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(
            4, 3, GL_FLOAT, GL_FALSE, sizeof(GPUInstance), (void*)offset
    );
    glVertexAttribDivisor(4, 1); // per instance

    // Attribute 5: opacity (float)
    offset += sizeof(glm::vec3);
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(
            5, 1, GL_FLOAT, GL_FALSE, sizeof(GPUInstance), (void*)offset
    );
    glVertexAttribDivisor(5, 1); // per instance

    // Attribute 6: uv (vec2)
    offset += sizeof(float);
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(
            6, 2, GL_FLOAT, GL_FALSE, sizeof(GPUInstance), (void*)offset
    );
    glVertexAttribDivisor(6, 1); // per instance

    // Attribute 7: layer (float)
    offset += sizeof(glm::vec2);
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(
            7, 1, GL_FLOAT, GL_FALSE, sizeof(GPUInstance), (void*)offset
    );
    glVertexAttribDivisor(7, 1); // per instance


    // Cleanup
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void GLSplatRenderer::RemoveSplats(int cx, int cz) {
    std::pair<int,int> key{cx, cz};
    auto it = buffers.find(key);
    if (it == buffers.end()) {
        return;
    }

    ChunkSplatBuffer& buf = it->second;
    if (buf.vbo) {
        glDeleteBuffers(1, &buf.vbo);
    }
    if (buf.vao) {
        glDeleteVertexArrays(1, &buf.vao);
    }

    buffers.erase(it);
}

bool GLSplatRenderer::LoadTrainedChunkFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "[Splats] Failed to open: " << path << std::endl;
        return false;
    }

    SplatBinHeader h;
    if (!readHeader(in, h)) {
        std::cerr << "[Splats] Failed to read header: " << path << std::endl;
        return false;
    }

    constexpr uint32_t MAGIC_SPL2 = 0x53504C32; // 'SPL2'
    if (h.magic != MAGIC_SPL2) {
        std::cerr << "[Splats] Bad magic in " << path << std::endl;
        return false;
    }
    if (h.version != 2) {
        std::cerr << "[Splats] Unsupported version " << h.version << " in " << path << std::endl;
        return false;
    }

    const size_t bytes = static_cast<size_t>(h.count) * static_cast<size_t>(h.stride);
    std::vector<uint8_t> raw;
    raw.resize(bytes);
    if (bytes > 0) {
        if (!in.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(bytes))) {
            std::cerr << "[Splats] Failed to read payload in " << path << std::endl;
            return false;
        }
    }

    std::vector<Splat> decoded;
    decodeToSplats(h, raw, decoded);

    // Upload using the existing VAO/VBO instancing path.
    UploadSplats(h.cx, h.cz, decoded);
    buffers[std::make_pair(h.cx, h.cz)].trained = true;

    std::cout << "[Splats] Loaded trained chunk (" << h.cx << "," << h.cz << ") count=" << decoded.size()
              << " stride=" << h.stride << " from " << path << std::endl;

    return true;
}

bool GLSplatRenderer::LoadTrainedChunkFolder(const std::string& folder) {
    namespace fs = std::filesystem;
    if (!fs::exists(folder)) {
        std::cerr << "[Splats] Folder does not exist: " << folder << std::endl;
        return false;
    }

    std::vector<fs::path> files;
    for (const auto& it : fs::directory_iterator(folder)) {
        if (!it.is_regular_file()) continue;
        const auto& p = it.path();
        const std::string name = p.filename().string();
        // Only accept our naming convention; header is still authoritative.
        if (name.rfind("chunk_", 0) == 0 && name.find(".splats.bin") != std::string::npos) {
            files.push_back(p);
        }
    }

    std::sort(files.begin(), files.end());

    size_t loaded = 0;
    for (const auto& p : files) {
        if (LoadTrainedChunkFile(p.string())) {
            loaded++;
        }
    }

    std::cout << "[Splats] Loaded trained chunks from folder: " << folder
              << " (" << loaded << "/" << files.size() << ")" << std::endl;

    return loaded > 0;
}

void GLSplatRenderer::SetLighting(const glm::mat4& lightViewProj, GLuint shadowTexture, const glm::vec3& lightDir)
{
    m_LightViewProj = lightViewProj;
    m_ShadowTexture = shadowTexture;
    m_LightDir      = lightDir;
}

void GLSplatRenderer::Draw(const glm::mat4& viewProj,
                           int camChunkX, int camChunkZ,
                           int maxChunkDistance)
{
    if (!shaderProgram) {
        return;
    }

    glUseProgram(shaderProgram);

    // Set view-projection matrix for the current camera
    GLint loc = glGetUniformLocation(shaderProgram, "uViewProj");
    if (loc >= 0) {
        glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(viewProj));
    }

    // Set light view-projection matrix for shadow mapping
    loc = glGetUniformLocation(shaderProgram, "lightViewProj");
    if (loc >= 0) {
        glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(m_LightViewProj));
    }

    // Set light direction
    loc = glGetUniformLocation(shaderProgram, "lightDir");
    if (loc >= 0) {
        glUniform3fv(loc, 1, glm::value_ptr(m_LightDir));
    }

    // Bind shadow map texture to unit 1 and set sampler
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_ShadowTexture);
    loc = glGetUniformLocation(shaderProgram, "uShadowMap");
    if (loc >= 0) {
        glUniform1i(loc, 1);
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    for (const auto& entry : buffers) {
        const std::pair<int,int>& key = entry.first;
        const int cx = key.first;
        const int cz = key.second;

        int dx = cx - camChunkX;
        int dz = cz - camChunkZ;
        if (std::max(std::abs(dx), std::abs(dz)) > maxChunkDistance) {
            continue;
        }

        const ChunkSplatBuffer& buf = entry.second;
        if (buf.vao == 0 || buf.count == 0) {
            continue;
        }

        glBindVertexArray(buf.vao);

        // 6 vertices for the quad, instanced per splat
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, buf.count);
    }

    glDepthMask(GL_TRUE);
    glBindVertexArray(0);
    glUseProgram(0);
}

GLuint GLSplatRenderer::createShaderProgram() {
    // Load vertex and fragment shader sources from external files
    // Adjust the paths to match your shader directory layout
    std::string vertSource = loadShaderSource("assets/shaders/splat.vsh");
    std::string fragSource = loadShaderSource("assets/shaders/splat.fsh");

    if (vertSource.empty() || fragSource.empty()) {
        std::cerr << "Failed to load splat shader sources." << std::endl;
        return 0;
    }

    const char* vertSrc = vertSource.c_str();
    const char* fragSrc = fragSource.c_str();

    GLuint vs = compileShader(GL_VERTEX_SHADER, vertSrc);
    if (!vs) {
        std::cerr << "Failed to compile splat vertex shader\n";
        return 0;
    }

    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragSrc);
    if (!fs) {
        std::cerr << "Failed to compile splat fragment shader\n";
        glDeleteShader(vs);
        return 0;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint status = GL_FALSE;
    glGetProgramiv(prog, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
        GLint logLength = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &logLength);
        std::string log(logLength, '\0');
        glGetProgramInfoLog(prog, logLength, nullptr, log.data());
        std::cerr << "Failed to link splat shader program:\n" << log << std::endl;
        glDeleteProgram(prog);
        return 0;
    }

    return prog;
}

GLuint GLSplatRenderer::compileShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint status = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
        GLint logLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        std::string log(logLength, '\0');
        glGetShaderInfoLog(shader, logLength, nullptr, log.data());
        std::cerr << "Failed to compile shader:\n" << log << std::endl;
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}



void GLSplatRenderer::initQuad()
{
    if (m_QuadVbo != 0)
        return;

    // 2 triangles, 6 corners, each vec2 (x,y) in [-1,1]
    const float corners[] = {
            -1.f, -1.f,
            1.f, -1.f,
            1.f,  1.f,
            -1.f, -1.f,
            1.f,  1.f,
            -1.f,  1.f
    };

    glGenBuffers(1, &m_QuadVbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_QuadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(corners), corners, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}
