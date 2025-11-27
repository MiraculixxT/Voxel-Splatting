#include "render/core/ViewFrustum.hpp"

void ViewFrustum::Update(const glm::mat4 &projViewMatrix) {
    // Left plane
    m_Planes[0].normal.x = projViewMatrix[0][3] + projViewMatrix[0][0];
    m_Planes[0].normal.y = projViewMatrix[1][3] + projViewMatrix[1][0];
    m_Planes[0].normal.z = projViewMatrix[2][3] + projViewMatrix[2][0];
    m_Planes[0].distance = projViewMatrix[3][3] + projViewMatrix[3][0];

    // Right plane
    m_Planes[1].normal.x = projViewMatrix[0][3] - projViewMatrix[0][0];
    m_Planes[1].normal.y = projViewMatrix[1][3] - projViewMatrix[1][0];
    m_Planes[1].normal.z = projViewMatrix[2][3] - projViewMatrix[2][0];
    m_Planes[1].distance = projViewMatrix[3][3] - projViewMatrix[3][0];

    // Bottom plane
    m_Planes[2].normal.x = projViewMatrix[0][3] + projViewMatrix[0][1];
    m_Planes[2].normal.y = projViewMatrix[1][3] + projViewMatrix[1][1];
    m_Planes[2].normal.z = projViewMatrix[2][3] + projViewMatrix[2][1];
    m_Planes[2].distance = projViewMatrix[3][3] + projViewMatrix[3][1];

    // Top plane
    m_Planes[3].normal.x = projViewMatrix[0][3] - projViewMatrix[0][1];
    m_Planes[3].normal.y = projViewMatrix[1][3] - projViewMatrix[1][1];
    m_Planes[3].normal.z = projViewMatrix[2][3] - projViewMatrix[2][1];
    m_Planes[3].distance = projViewMatrix[3][3] - projViewMatrix[3][1];

    // Near plane
    m_Planes[4].normal.x = projViewMatrix[0][3] + projViewMatrix[0][2];
    m_Planes[4].normal.y = projViewMatrix[1][3] + projViewMatrix[1][2];
    m_Planes[4].normal.z = projViewMatrix[2][3] + projViewMatrix[2][2];
    m_Planes[4].distance = projViewMatrix[3][3] + projViewMatrix[3][2];

    // Far plane
    m_Planes[5].normal.x = projViewMatrix[0][3] - projViewMatrix[0][2];
    m_Planes[5].normal.y = projViewMatrix[1][3] - projViewMatrix[1][2];
    m_Planes[5].normal.z = projViewMatrix[2][3] - projViewMatrix[2][2];
    m_Planes[5].distance = projViewMatrix[3][3] - projViewMatrix[3][2];

    for (auto &plane: m_Planes) {
        plane.normalize();
    }
}

bool ViewFrustum::IsBoxVisible(const glm::vec3 &min, const glm::vec3 &max) const {
    for (const auto& plane : m_Planes) {
        // Check if the "positive vertex" (the corner furthest in the direction of the normal)
        // is behind the plane. If it is, the box is completely outside.
        glm::vec3 pVertex = min;
        if (plane.normal.x >= 0) pVertex.x = max.x;
        if (plane.normal.y >= 0) pVertex.y = max.y;
        if (plane.normal.z >= 0) pVertex.z = max.z;

        if (plane.distanceToPoint(pVertex) < 0) {
            return false;
        }
    }
    return true;
}
