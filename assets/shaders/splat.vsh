#version 450 core
layout(location = 0) in vec2 quadPos; // (-1,-1) to (1,1)
layout(location = 1) in vec3 center;
layout(location = 2) in vec3 scale;
layout(location = 3) in vec4 rot; // Quaternion (xyzw)
layout(location = 4) in vec4 color;

uniform mat4 view;
uniform mat4 projection;
uniform mat4 splatModel;

out vec2 v_uv;
out vec4 v_color;

// Helper to rotate a vector by a quaternion
vec3 rotate_quat(vec3 v, vec4 q) {
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

void main() {
    v_uv = quadPos;
    v_color = color;

    // 1. Get Camera Right and Up vectors for billboarding
    vec3 right = vec3(view[0][0], view[1][0], view[2][0]);
    vec3 up    = vec3(view[0][1], view[1][1], view[2][1]);

    // 2. Apply Rotation and Scale to the quad vertices
    // We treat the quad as a flat plane in 3D space then rotate it
    vec3 localPos = vec3(quadPos.x * scale.x, quadPos.y * scale.y, 0.0);
    vec3 rotatedPos = rotate_quat(localPos, rot);

    // Apply global transform to the center position
    vec4 worldCenter = splatModel * vec4(center, 1.0);
    vec3 worldPos = worldCenter.xyz + (right * rotatedPos.x) + (up * rotatedPos.y);

    gl_Position = projection * view * vec4(worldPos, 1.0);
}