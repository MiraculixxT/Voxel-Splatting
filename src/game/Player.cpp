//
// Created by Tim Müller on 19.11.25.
//
#include "game/Player.hpp"
#include "render/core/Camera.hpp"
#include "game/World.hpp"
#include "game/Block.hpp"
#include <cmath>
#include <limits>

Player::Player(Camera* camera, World* world)
        : Position(8.0f, 60.0f, 24.0f), m_Camera(camera), m_World(world)
{
    m_Camera->Position = Position + glm::vec3(0,1.7f,0);

    // Initialize hotbar inventory as empty
    for (int i = 0; i < HOTBAR_SIZE; ++i) {
        m_Hotbar[i].type = BlockType::Air;
        m_Hotbar[i].count = 0;
    }
    m_SelectedHotbarIndex = 0;
}

void Player::Update(const float dt) {
    if (!IsFlying) ApplyGravity(dt);

    // Camera follows player
    m_Camera->Position = Position + glm::vec3(0, 1.7f, 0);
}

void Player::ProcessInput(GLFWwindow* window, const float dt) {
    HandleHotbarSelectionInput(window);
    ApplyMovement(window, dt);
    ApplyBlockInteraction(window, dt);
}

void Player::HandleHotbarSelectionInput(GLFWwindow* window) {
    if (glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS) m_SelectedHotbarIndex = 0;
    else if (glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS) m_SelectedHotbarIndex = 1;
    else if (glfwGetKey(window, GLFW_KEY_3) == GLFW_PRESS) m_SelectedHotbarIndex = 2;
    else if (glfwGetKey(window, GLFW_KEY_4) == GLFW_PRESS) m_SelectedHotbarIndex = 3;
    else if (glfwGetKey(window, GLFW_KEY_5) == GLFW_PRESS) m_SelectedHotbarIndex = 4;
    else if (glfwGetKey(window, GLFW_KEY_6) == GLFW_PRESS) m_SelectedHotbarIndex = 5;
    else if (glfwGetKey(window, GLFW_KEY_7) == GLFW_PRESS) m_SelectedHotbarIndex = 6;
    else if (glfwGetKey(window, GLFW_KEY_8) == GLFW_PRESS) m_SelectedHotbarIndex = 7;
    else if (glfwGetKey(window, GLFW_KEY_9) == GLFW_PRESS) m_SelectedHotbarIndex = 8;
}

bool Player::AddBlockToInventory(BlockType type) {
    if (type == BlockType::Air)
        return false;

    // First: try to stack onto existing stacks of the same type
    for (int i = 0; i < HOTBAR_SIZE; ++i) {
        InventorySlot& slot = m_Hotbar[i];
        if (!slot.empty() && slot.type == type && slot.count < 64) {
            ++slot.count;
            return true;
        }
    }

    // Second: put into an empty slot
    for (int i = 0; i < HOTBAR_SIZE; ++i) {
        InventorySlot& slot = m_Hotbar[i];
        if (slot.empty()) {
            slot.type = type;
            slot.count = 1;
            return true;
        }
    }

    // Inventory full
    return false;
}

BlockType Player::GetSelectedBlockType() const {
    const InventorySlot& slot = m_Hotbar[m_SelectedHotbarIndex];
    if (slot.empty())
        return BlockType::Air;
    return slot.type;
}

bool Player::RemoveBlockFromSelectedSlot() {
    InventorySlot& slot = m_Hotbar[m_SelectedHotbarIndex];
    if (slot.empty())
        return false;

    --slot.count;
    if (slot.count <= 0) {
        slot.count = 0;
        slot.type = BlockType::Air;
    }
    return true;
}

void Player::ApplyBlockInteraction(GLFWwindow *window, float dt) {
    if (cooldownBreak > 0.0f) cooldownBreak -= dt;
    if (cooldownPlace > 0.0f) cooldownPlace -= dt;

    if (cooldownBreak <= 0.0f && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        // Break block
        if (const RaycastResult result = Raycast(5.0f); result.hit) {
            BlockState currentBlock = m_World->getBlock(result.blockPos.x, result.blockPos.y, result.blockPos.z);

            if (currentBlock.type != BlockType::Air) {
                AddBlockToInventory(currentBlock.type);
            }

            cooldownBreak = 0.2f; // 200ms cooldown
            m_World->setBlock(result.blockPos.x, result.blockPos.y, result.blockPos.z, BlockType::Air);
        }
    }
    else if (cooldownPlace <= 0.0f && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
        // Place block from selected hotbar slot
        BlockType placeType = GetSelectedBlockType();
        if (placeType != BlockType::Air) {
            if (const RaycastResult result = Raycast(5.0f); result.hit) {
                if (m_World->setBlock(result.prevBlockPos.x, result.prevBlockPos.y, result.prevBlockPos.z, placeType)) {
                    RemoveBlockFromSelectedSlot();
                    cooldownPlace = 0.2f; // 200ms cooldown
                }
            }
        }
    }
}


void Player::ApplyMovement(GLFWwindow* window, float dt) {
    glm::vec3 forward = m_Camera->Front;
    forward.y = 0.0f;
    if (glm::length(forward) > 0.0001f)
        forward = glm::normalize(forward);

    glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));

    glm::vec3 move(0.0f);
    const bool isSprinting = (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS);

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        move += forward;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        move -= forward;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        move -= right;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        move += right;

    float vSpeed = Speed*2 + (IsFlying * Speed * m_Camera->MovementSpeed / 2);
    float hSpeed = Speed + (IsFlying * Speed * m_Camera->MovementSpeed) + (isSprinting * Speed);
    if (glm::length(move) > 0.0001f) {
        move = glm::normalize(move) * hSpeed * dt;

        // X axis
        glm::vec3 newPos = Position;
        newPos.x += move.x;
        if (IsFlying || !IsCollidingAt(newPos)) {
            Position.x = newPos.x;
        }

        // Z axis
        newPos = Position;
        newPos.z += move.z;
        if (IsFlying || !IsCollidingAt(newPos)) {
            Position.z = newPos.z;
        }
    }

    // Jump (only allowed when on ground, flag is set in gravity handling)
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
        if (IsFlying) {
            Position.y += vSpeed * 2 * dt;
        } else if (OnGround) {
            VelocityY = 10.0f;
            OnGround = false;
        }
    }

    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
        if (IsFlying) {
            Position.y -= vSpeed * 2 * dt;
        }
    }
}

void Player::ApplyGravity(float dt) {
    VelocityY += Gravity * dt;
    if (VelocityY < -50.0f)
        VelocityY = -50.0f; // clamp fall speed

    glm::vec3 newPos = Position;
    newPos.y += VelocityY * dt;

    if (VelocityY <= 0.0f) {
        // Falling or standing
        if (!IsCollidingAt(newPos)) {
            Position = newPos;
            OnGround = false;
        } else {
            // Hit the ground: find the ground height just below the player
            const float halfWidth = 0.3f;
            int x0 = static_cast<int>(std::floor(Position.x - halfWidth));
            int x1 = static_cast<int>(std::floor(Position.x + halfWidth));
            int z0 = static_cast<int>(std::floor(Position.z - halfWidth));
            int z1 = static_cast<int>(std::floor(Position.z + halfWidth));

            int maxGroundY = std::numeric_limits<int>::min();
            int startY = static_cast<int>(std::floor(Position.y));

            for (int x = x0; x <= x1; ++x) {
                for (int z = z0; z <= z1; ++z) {
                    for (int y = startY; y >= startY - 4; --y) { // search a few blocks down
                        if (IsBlockSolid(x, y, z)) {
                            if (y > maxGroundY)
                                maxGroundY = y;
                            break;
                        }
                    }
                }
            }

            if (maxGroundY != std::numeric_limits<int>::min()) {
                // Place feet just above the highest solid block
                Position.y = static_cast<float>(maxGroundY + 1);
                VelocityY = 0.0f;
                OnGround = true;
            } else {
                // Fallback: don't move further down
                VelocityY = 0.0f;
            }
        }
    } else {
        // Moving upwards (jumping)
        if (!IsCollidingAt(newPos)) {
            Position = newPos;
        } else {
            // Hit ceiling
            VelocityY = 0.0f;
        }
        OnGround = false;
    }
}

bool Player::IsBlockSolid(int x, int y, int z) const {
    if (!m_World)
        return false;

    BlockState block = m_World->getBlock(x, y, z);
    return !BlockDatabase::IsTransparent(block);
}

bool Player::IsCollidingAt(const glm::vec3& pos) const {
    const float halfWidth = 0.3f;
    const float height = 1.7f;

    glm::vec3 min = pos + glm::vec3(-halfWidth, 0.0f, -halfWidth);
    glm::vec3 max = pos + glm::vec3( halfWidth, height,  halfWidth);

    int x0 = static_cast<int>(std::floor(min.x));
    int x1 = static_cast<int>(std::floor(max.x));
    int y0 = static_cast<int>(std::floor(min.y));
    int y1 = static_cast<int>(std::floor(max.y));
    int z0 = static_cast<int>(std::floor(min.z));
    int z1 = static_cast<int>(std::floor(max.z));

    for (int x = x0; x <= x1; ++x) {
        for (int y = y0; y <= y1; ++y) {
            for (int z = z0; z <= z1; ++z) {
                if (IsBlockSolid(x, y, z))
                    return true;
            }
        }
    }

    return false;
}

RaycastResult Player::Raycast(float maxDistance) const {
    RaycastResult result;
    glm::vec3 origin = m_Camera->Position;
    glm::vec3 direction = m_Camera->Front;

    glm::ivec3 currentBlockPos = glm::ivec3(floor(origin.x), floor(origin.y), floor(origin.z));
    result.prevBlockPos = currentBlockPos;

    glm::vec3 step = glm::normalize(direction);
    float t = 0.0f;

    while (t < maxDistance) {
        t += 0.01f;
        glm::vec3 pos = origin + t * direction;
        glm::ivec3 newBlockPos = glm::ivec3(floor(pos.x), floor(pos.y), floor(pos.z));

        if (newBlockPos != currentBlockPos) {
            result.prevBlockPos = currentBlockPos;
            currentBlockPos = newBlockPos;
            if (IsBlockSolid(currentBlockPos.x, currentBlockPos.y, currentBlockPos.z)) {
                result.hit = true;
                result.blockPos = currentBlockPos;
                return result;
            }
        }
    }

    return result;
}
