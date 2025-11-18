#pragma once
#include <glm/glm.hpp>
#include <array>

struct Plane {
    float distanceToPoint(const glm::vec3& point) const {
        return glm::dot(normal, point) + distance;
    }

    glm::vec3 normal = { 0.f, 1.f, 0.f };
    float distance = 0.f;

    // Normalize the plane for accurate distance checks
    void normalize() {
        const float mag = glm::length(normal);
        normal /= mag;
        distance /= mag;
    }
};

class ViewFrustum {
public:
    std::array<Plane, 6> m_Planes;

    void Update(const glm::mat4& projViewMatrix);

    // Check if an AABB (Axis Aligned Bounding Box) is in the frustum
    bool IsBoxVisible(const glm::vec3& min, const glm::vec3& max) const;
};
