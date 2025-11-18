#version 410 core
out vec4 FragColor;

// (u, v, layer)
in vec3 TexCoord;
in vec3 WorldPos;

// Use a 2D Array Sampler
uniform sampler2DArray textureArray;
uniform vec3 cameraPosition;
uniform float time;
uniform float fogStart;
uniform float fogEnd;


void main() {
    // Sample the 2D array
    vec4 texColor = texture(textureArray, TexCoord);
    if (texColor.a < 0.1) discard;

    // FOG
    float dist = distance(WorldPos, cameraPosition);
    float fogFactor = 0.0;
    if (fogEnd > fogStart) {
        fogFactor = clamp((dist - fogStart) / (fogEnd - fogStart), 0.0, 1.0);
    }
    vec3 fogColor = vec3(0.5f, 0.8f, 1.0f); // Sky color
    vec3 fogMixed = mix(texColor.rgb, fogColor, fogFactor);

    FragColor = vec4(fogMixed, texColor.a);
}