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
}

void Player::Update(const float dt) {
    if (!IsFlying) ApplyGravity(dt);

    // Camera follows player
    m_Camera->Position = Position + glm::vec3(0, 1.7f, 0);
}

void Player::ProcessInput(GLFWwindow* window, const float dt) {
    ApplyMovement(window, dt);
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