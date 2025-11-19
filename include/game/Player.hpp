#pragma once
#include <glm/glm.hpp>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>


class Camera;
class World;

class Player {
public:
    Player(Camera* camera, World* world);

    void Update(float dt);
    void ProcessInput(GLFWwindow* window, float dt);

    glm::vec3 Position;
    float Speed = 4.0f;
    float VelocityY = 0.0f;
    float Gravity = -35.0f;
    bool OnGround = false;

    bool IsCollidingAt(const glm::vec3& pos) const;
    bool IsBlockSolid(int x, int y, int z) const;

private:
    Camera* m_Camera;
    World* m_World;

    void ApplyGravity(float dt);
    void ApplyMovement(GLFWwindow* window, float dt);
};