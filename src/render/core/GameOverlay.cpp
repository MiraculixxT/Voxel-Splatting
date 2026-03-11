// ReSharper disable CppDFAUnreachableCode
#include "render/core/GameOverlay.hpp"
#include <iostream>
#include "glm/gtc/matrix_transform.hpp"
#include "game/Player.hpp"

GameOverlay::GameOverlay()
    : m_Shader(),
      m_TextShader(),
      m_HotbarTexture("assets/overlay/inv.png", GLTexture::FilterMode::PixelPerfect),
      m_SelectedSlotTexture("assets/overlay/selected.png", GLTexture::FilterMode::PixelPerfect),
      m_NumberAtlasTexture("assets/overlay/numbers.png", GLTexture::FilterMode::PixelPerfect),
      m_Vao(0), m_Vbo(0), m_Ebo(0),
      m_Width(0), m_Height(0),
      m_HotbarScale(1.0f), m_HotbarWidth(0.0f), m_HotbarHeight(0.0f), m_HotbarX(0.0f), m_HotbarY(0.0f),
      m_Player(nullptr)
{
    for (int i = 0; i < kBlockTypeCount; ++i) {
        m_BlockIconTextures[i] = nullptr;
    }
}

GameOverlay::~GameOverlay() {
    if (m_Vao) glDeleteVertexArrays(1, &m_Vao);
    if (m_Vbo) glDeleteBuffers(1, &m_Vbo);
    if (m_Ebo) glDeleteBuffers(1, &m_Ebo);
}

void GameOverlay::Init(int screenWidth, int screenHeight) {
    m_Width = screenWidth;
    m_Height = screenHeight;

    m_Shader.Init("assets/shaders/overlay.vsh", "assets/shaders/overlay.fsh");
    // Reuse same vertex shader; fragment shader samples a number atlas and uses a digit index uniform
    m_TextShader.Init("assets/shaders/overlay.vsh", "assets/shaders/overlay_text.fsh");

    if (!m_HotbarTexture.Load()) {
        std::cerr << "[GameOverlay] Failed to load hotbar texture at assets/overlay/inv.png" << std::endl;
    }
    if (!m_SelectedSlotTexture.Load()) {
        std::cerr << "[GameOverlay] Failed to load selected slot texture at assets/overlay/selected.png" << std::endl;
    }
    if (!m_NumberAtlasTexture.Load()) {
        std::cerr << "[GameOverlay] Failed to load number atlas at assets/overlay/numbers.png" << std::endl;
    }

    auto loadIcon = [this](BlockType type, const char* path) {
        const int idx = static_cast<int>(type);
        if (idx < 0 || idx >= kBlockTypeCount) return;
        GLTexture* tex = new GLTexture(path, GLTexture::FilterMode::PixelPerfect);
        if (!tex->Load()) {
            std::cerr << "[GameOverlay] Failed to load icon texture " << path << std::endl;
            delete tex;
            tex = nullptr;
        }
        m_BlockIconTextures[idx] = tex;
    };

    loadIcon(BlockType::Dirt, "assets/overlay/dirt.png");
    loadIcon(BlockType::Grass, "assets/overlay/grass_top.png");
    loadIcon(BlockType::Stone, "assets/overlay/stone.png");
    loadIcon(BlockType::Sand, "assets/overlay/sand.png");
    loadIcon(BlockType::Snow, "assets/overlay/snow.png");
    loadIcon(BlockType::Wood, "assets/overlay/log_top.png");
    loadIcon(BlockType::Leaves, "assets/overlay/leave.png");
    loadIcon(BlockType::Water, "assets/textures/water.png");

    setupQuad();
}

void GameOverlay::setupQuad() {
    // Simple unit quad in local space (0..1), we will scale/translate in model matrix
    const float vertices[] = {
        // pos    // tex
        0.0f, 0.0f, 0.0f, 0.0f, // bottom-left
        1.0f, 0.0f, 1.0f, 0.0f, // bottom-right
        1.0f, 1.0f, 1.0f, 1.0f, // top-right
        0.0f, 1.0f, 0.0f, 1.0f  // top-left
    };
    const unsigned int indices[] = {
        0, 1, 2,
        2, 3, 0
    };

    glGenVertexArrays(1, &m_Vao);
    glGenBuffers(1, &m_Vbo);
    glGenBuffers(1, &m_Ebo);

    glBindVertexArray(m_Vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_Vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_Ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glBindVertexArray(0);
}

void GameOverlay::OnFramebufferSize(int width, int height) {
    m_Width = width;
    m_Height = height;

    // Define how big the hotbar is relative to the screen (e.g., 40% of width or fixed size)
    // This depends on your GL texture rendering logic, but let's assume a fixed scale here:
    float aspect = 182.0f / 22.0f; // Typical Minecraft hotbar aspect ratio

    // Example: Set width to 400 pixels (or scale based on screen width)
    m_HotbarWidth = 400.0f;
    m_HotbarHeight = m_HotbarWidth / aspect;

    // CENTER X: (ScreenWidth - BarWidth) / 2
    m_HotbarX = (m_Width - m_HotbarWidth) * 0.5f;

    // BOTTOM Y: ScreenHeight - BarHeight - Padding
    m_HotbarY = m_Height - m_HotbarHeight - 10.0f;
}

void GameOverlay::Render() {
    if (m_Width == 0 || m_Height == 0) return; // not initialized yet

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    const glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(m_Width),
                                            0.0f, static_cast<float>(m_Height));

    glBindVertexArray(m_Vao);

    m_Shader.use();
    m_Shader.setMat4("projection", projection);

    RenderCrosshair();
    RenderHotbar();

    glBindVertexArray(0);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void GameOverlay::RenderCrosshair() {
    // Render a plus-shaped crosshair at the center using two thin quads
    const float centerX = m_Width * 0.5f;
    const float centerY = m_Height * 0.5f;

    // Crosshair dimensions in screen space (pixels)
    const float crossLength    = 16.0f; // total length of each bar
    const float crossThickness = 2.0f;  // thickness of the bars

    m_Shader.setInt("useTexture", 0);
    m_Shader.setVec3("color", glm::vec3(1.0f));

    // Vertical bar
    {
        glm::mat4 model(1.0f);
        const float x = centerX - crossThickness * 0.5f;
        const float y = centerY - crossLength * 0.5f;
        model = glm::translate(model, glm::vec3(x, y, 0.0f));
        model = glm::scale(model, glm::vec3(crossThickness, crossLength, 1.0f));
        m_Shader.setMat4("model", model);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
    }

    // Horizontal bar
    {
        glm::mat4 model(1.0f);
        const float x = centerX - crossLength * 0.5f;
        const float y = centerY - crossThickness * 0.5f;
        model = glm::translate(model, glm::vec3(x, y, 0.0f));
        model = glm::scale(model, glm::vec3(crossLength, crossThickness, 1.0f));
        m_Shader.setMat4("model", model);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
    }
}

void GameOverlay::GetSlotCenter(int index, float &outX, float &outY) const {
    const float singleSlotWidth = m_HotbarWidth / 9.0f;
    const float slotLeft = m_HotbarX + (index * singleSlotWidth);
    outX = slotLeft + (singleSlotWidth * 0.5f);
    outY = m_HotbarY + (m_HotbarHeight * 0.5f);
}

void GameOverlay::RenderHotbar() {
    // Scale the hotbar relative to screen width so it resizes nicely
    const float baseHotbarWidth = 364.0f;
    const float baseHotbarHeight = 44.0f;

    // Reference width 1280 -> scale factor
    m_HotbarScale = static_cast<float>(m_Width) / 1280.0f;
    m_HotbarWidth = baseHotbarWidth * m_HotbarScale;
    m_HotbarHeight = baseHotbarHeight * m_HotbarScale;

    m_HotbarX = (m_Width - m_HotbarWidth) * 0.5f;
    m_HotbarY = 16.0f * m_HotbarScale; // margin from bottom

    // Draw hotbar background (inv.png)
    glm::mat4 model(1.0f);
    model = glm::translate(model, glm::vec3(m_HotbarX, m_HotbarY, 0.0f));
    model = glm::scale(model, glm::vec3(m_HotbarWidth, m_HotbarHeight, 1.0f));

    m_Shader.setMat4("model", model);
    m_Shader.setInt("useTexture", 1);

    m_HotbarTexture.Bind();
    m_Shader.setInt("screenTexture", 0);

    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);

    if (!m_Player) {
        return;
    }

    const int selectedIndex = m_Player->GetSelectedHotbarIndex();
    const int slotCount = Player::HOTBAR_SIZE;

    // Slot dimensions from cached layout
    const float slotWidth  = m_HotbarWidth  / static_cast<float>(slotCount);
    const float slotHeight = m_HotbarHeight;

    // Compute size of selection quad based on selected.png aspect ratio
    const float selTexW = static_cast<float>(m_SelectedSlotTexture.GetWidth());
    const float selTexH = static_cast<float>(m_SelectedSlotTexture.GetHeight());
    float selAspect = 1.0f;
    if (selTexH > 0.0f) selAspect = selTexW / selTexH;

    float selHeight = slotHeight * 0.9f;
    float selWidth = selHeight * selAspect;
    if (selWidth > slotWidth * 0.9f) {
        selWidth = slotWidth * 0.9f;
        selHeight = selWidth / selAspect;
    }

    float centerX, centerY;
    GetSlotCenter(selectedIndex, centerX, centerY);

    const float selX = centerX - selWidth * 0.5f;
    const float selY = centerY - selHeight * 0.5f;

    glm::mat4 selModel(1.0f);
    selModel = glm::translate(selModel, glm::vec3(selX, selY, 0.0f));
    selModel = glm::scale(selModel, glm::vec3(selWidth, selHeight, 1.0f));

    m_Shader.setMat4("model", selModel);
    m_Shader.setInt("useTexture", 1);
    m_SelectedSlotTexture.Bind();
    m_Shader.setInt("screenTexture", 0);

    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);

    // Draw item icons in each slot
    for (int i = 0; i < slotCount; ++i) {
        const InventorySlot& slot = m_Player->GetSlot(i);
        if (slot.empty()) continue;

        const int idx = static_cast<int>(slot.type);
        if (idx < 0 || idx >= kBlockTypeCount) continue;
        GLTexture* tex = m_BlockIconTextures[idx];
        if (!tex) continue;

        float iconCenterX, iconCenterY;
        GetSlotCenter(i, iconCenterX, iconCenterY);

        const float iconTexW = static_cast<float>(tex->GetWidth());
        const float iconTexH = static_cast<float>(tex->GetHeight());
        float iconAspect = 1.0f;
        if (iconTexH > 0.0f) iconAspect = iconTexW / iconTexH;

        float iconHeight = slotHeight * 0.6f;
        float iconWidth = iconHeight * iconAspect;
        if (iconWidth > slotWidth * 0.8f) {
            iconWidth = slotWidth * 0.8f;
            iconHeight = iconWidth / iconAspect;
        }

        const float iconX = iconCenterX - iconWidth * 0.5f;
        const float iconY = iconCenterY - iconHeight * 0.5f;

        glm::mat4 iconModel(1.0f);
        iconModel = glm::translate(iconModel, glm::vec3(iconX, iconY, 0.0f));
        iconModel = glm::scale(iconModel, glm::vec3(iconWidth, iconHeight, 1.0f));

        m_Shader.setMat4("model", iconModel);
        m_Shader.setInt("useTexture", 1);
        tex->Bind();
        m_Shader.setInt("screenTexture", 0);

        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
    }

    // After drawing icons, draw numbers on top using text shader
    RenderHotbarNumbers();
}

void GameOverlay::RenderHotbarNumbers() {
    if (!m_Player) return;

    const int slotCount = Player::HOTBAR_SIZE;
    if (slotCount <= 0) return;

    const float slotWidth  = m_HotbarWidth  / static_cast<float>(slotCount);
    const float slotHeight = m_HotbarHeight;

    m_TextShader.use();
    const glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(m_Width),
                                            0.0f, static_cast<float>(m_Height));
    m_TextShader.setMat4("projection", projection);
    m_TextShader.setInt("numberAtlas", 0);

    m_NumberAtlasTexture.Bind(GL_TEXTURE0);

    glBindVertexArray(m_Vao);

    for (int i = 0; i < slotCount; ++i) {
        const InventorySlot& slot = m_Player->GetSlot(i);

        // 1) Slot index (1–9) drawn just above the hotbar
        {
            const int digit = (i + 1) % 10; // 1..9
            m_TextShader.setInt("digit", digit);

            float centerX, centerY;
            GetSlotCenter(i, centerX, centerY);

            const float textWidth  = slotWidth * 0.3f;
            const float textHeight = slotHeight * 0.4f;

            // Place the index slightly above the top edge of the bar
            const float x = centerX - textWidth * 0.5f;
            const float y = m_HotbarY + m_HotbarHeight + 4.0f;

            glm::mat4 model(1.0f);
            model = glm::translate(model, glm::vec3(x, y, 0.0f));
            model = glm::scale(model, glm::vec3(textWidth, textHeight, 1.0f));

            m_TextShader.setMat4("model", model);
            //glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
        }

        if (slot.empty() || slot.count <= 0) continue;

        // 2) Stack count (up to two digits) in bottom-right of the slot
        int count = slot.count;
        if (count > 99) count = 99;

        const int ones = count % 10;
        const int tens = count / 10;

        const float digitWidth  = slotWidth * 0.22f;
        const float digitHeight = slotHeight * 0.35f;

        const float slotX0 = m_HotbarX + slotWidth * static_cast<float>(i);
        const float slotY0 = m_HotbarY;

        float xRight = slotX0 + slotWidth - digitWidth - 3.0f;
        const float yBottom = slotY0 + 3.0f;

        // Tens digit (if any)
        if (tens > 0) {
            m_TextShader.setInt("digit", tens);
            glm::mat4 modelT(1.0f);
            modelT = glm::translate(modelT, glm::vec3(xRight - digitWidth, yBottom, 0.0f));
            modelT = glm::scale(modelT, glm::vec3(digitWidth, digitHeight, 1.0f));
            m_TextShader.setMat4("model", modelT);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
        }

        // Ones digit
        m_TextShader.setInt("digit", ones);
        glm::mat4 modelO(1.0f);
        modelO = glm::translate(modelO, glm::vec3(xRight, yBottom, 0.0f));
        modelO = glm::scale(modelO, glm::vec3(digitWidth, digitHeight, 1.0f));
        m_TextShader.setMat4("model", modelO);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
    }
}
