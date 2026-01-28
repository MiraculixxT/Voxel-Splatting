#include "render/gl/GLSplatRenderer.hpp"

#include <iostream>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <fstream>
#include <sstream>
#include <limits>

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
    for (auto& entry : buffers) {
        ChunkSplatBuffer& buf = entry.second;
        if (buf.vbo) {
            glDeleteBuffers(1, &buf.vbo);
        }
        if (buf.vao) {
            glDeleteVertexArrays(1, &buf.vao);
        }
    }
    buffers.clear();

    if (shaderProgram) {
        glDeleteProgram(shaderProgram);
        shaderProgram = 0;
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

void GLSplatRenderer::UploadGlobalSplats(const std::vector<Splat>& splats) {
    // Use a sentinel key so global splats are always drawn.
    constexpr int kGlobalKey = std::numeric_limits<int>::max();
    UploadSplats(kGlobalKey, kGlobalKey, splats);
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

    for (const auto& entry : buffers) {
        const std::pair<int,int>& key = entry.first;
        const int cx = key.first;
        const int cz = key.second;

        const bool isGlobal = (cx == std::numeric_limits<int>::max() && cz == std::numeric_limits<int>::max());
        if (!isGlobal) {
            int dx = cx - camChunkX;
            int dz = cz - camChunkZ;
            if (std::max(std::abs(dx), std::abs(dz)) > maxChunkDistance) {
                continue;
            }
        }

        const ChunkSplatBuffer& buf = entry.second;
        if (buf.vao == 0 || buf.count == 0) {
            continue;
        }

        glBindVertexArray(buf.vao);

        // 6 vertices for the quad, instanced per splat
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, buf.count);
    }

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
