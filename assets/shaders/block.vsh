#version 410 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in float aTexLayer; // New vertex attribute

// Pass all 3 components to the fragment shader
out vec3 TexCoord; // (u, v, layer)
out vec3 WorldPos;
out vec4 LightSpacePos; // Light-space position for shadow mapping

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform float time;
uniform mat4 lightViewProj; // Light's view-projection matrix

void main() {
    vec3 localPos = aPos;
    if (int(aTexLayer + 0.5) == 6 && localPos.y > 0.99) { // water top surface only
        const float waveSpeed = 0.8;
        const float waveHeight = 0.08;
        float t = time * waveSpeed;
        float wave = sin((localPos.x + t) * 2.3) + cos((localPos.z - t) * 1.9);
        float wave2 = sin((localPos.x + localPos.z + t) * 1.3);
        float waveCombined = (wave + wave2) * 0.5;
        // Bias downward so the highest point stays at the block level.
        localPos.y += waveCombined * waveHeight - waveHeight;
    }

    vec4 worldPosition = model * vec4(localPos, 1.0);
    WorldPos = worldPosition.xyz;
    LightSpacePos = lightViewProj * worldPosition;
    gl_Position = projection * view * worldPosition;
    // Pass (u, v, layer)
    TexCoord = vec3(aTexCoord, aTexLayer);
}