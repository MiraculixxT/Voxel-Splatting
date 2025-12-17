#pragma once
#include <glm/glm.hpp>
#define GLFW_INCLUDE_NONE
#include <cstdio>
#include <GLFW/glfw3.h>
#include "game/Block.hpp"


struct RaycastResult {
    bool hit = false;
    glm::ivec3 blockPos;
    glm::ivec3 prevBlockPos;

    void print() const {
        printf("Result: hit=%d, block=(%d,%d,%d), prevBlock=(%d,%d,%d)\n",
            hit ? 1 : 0,
            blockPos.x, blockPos.y, blockPos.z,
            prevBlockPos.x, prevBlockPos.y, prevBlockPos.z);
    }
};

struct InventorySlot {
    BlockType type = BlockType::Air;
    int count = 0;

    bool empty() const { return count <= 0 || type == BlockType::Air; }
};

class Camera;
class World;

class Player {
public:
    Player(Camera* camera, World* world);

    void Update(float dt);
    void ProcessInput(GLFWwindow* window, float dt);
    void ApplyBlockInteraction(GLFWwindow *window, float dt);

    glm::vec3 Position;
    float Speed = 4.0f;
    float VelocityY = 0.0f;
    float Gravity = -35.0f;
    bool OnGround = false;
    bool IsFlying = false;

    bool IsCollidingAt(const glm::vec3& pos) const;
    bool IsBlockSolid(int x, int y, int z) const;

    // Inventory query helpers (useful for UI)
    static constexpr int HOTBAR_SIZE = 9;
    const InventorySlot& GetSlot(int index) const { return m_Hotbar[index]; }
    int GetSelectedHotbarIndex() const { return m_SelectedHotbarIndex; }
    BlockType GetSelectedBlockType() const;

private:
    Camera* m_Camera;
    World* m_World;

    void ApplyGravity(float dt);
    void ApplyMovement(GLFWwindow* window, float dt);

    RaycastResult Raycast(float maxDistance) const;

    void HandleHotbarSelectionInput(GLFWwindow* window);
    bool AddBlockToInventory(BlockType type);
    bool RemoveBlockFromSelectedSlot();

    float cooldownBreak = 0.0f;
    float cooldownPlace = 0.0f;

    InventorySlot m_Hotbar[HOTBAR_SIZE] = {};
    int m_SelectedHotbarIndex = 0; // 0..8 mapped to keys 1..9
};