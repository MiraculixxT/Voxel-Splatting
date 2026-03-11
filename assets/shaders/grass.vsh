#version 410 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in float aTexLayer;

out vec3 TexCoord;
out vec3 WorldPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform float time;

const float move_strength = 0.3;
const float move_speed = 2.0;

void main() {
    float t = time * move_speed;

    vec3 pos = aPos;
    float heightFactor = clamp(pos.y - floor(pos.y), 0.0, 1.0);
    float sway = sin(t * 1.7 + pos.x * 2.3 + pos.z * 2.1) * move_strength;
    pos.x += sway * heightFactor;
    pos.z += cos(t * 1.4 + pos.z * 2.5) * 0.05 * heightFactor;

    vec4 worldPosition = model * vec4(pos, 1.0);
    WorldPos = worldPosition.xyz;
    gl_Position = projection * view * worldPosition;
    TexCoord = vec3(aTexCoord, aTexLayer);
}
