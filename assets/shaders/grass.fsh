#version 410 core
out vec4 FragColor;

in vec3 TexCoord;
in vec3 WorldPos;

uniform sampler2DArray textureArray;
uniform vec3 cameraPosition;
uniform float fogStart;
uniform float fogEnd;

void main() {
    vec4 texColor = texture(textureArray, TexCoord);
    if (texColor.a < 0.1) discard;

    float dist = distance(WorldPos, cameraPosition);
    float fogFactor = 0.0;
    if (fogEnd > fogStart) {
        fogFactor = clamp((dist - fogStart) / (fogEnd - fogStart), 0.0, 1.0);
        fogFactor = fogFactor * fogFactor;
    }

    vec3 viewDir = normalize(WorldPos - cameraPosition);
    float tSky = clamp(viewDir.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 horizonColor = vec3(0.5, 0.8, 1.0);
    vec3 zenithColor  = vec3(0.15, 0.35, 0.9);
    vec3 fogColor = mix(horizonColor, zenithColor, tSky);

    vec3 fogMixed = mix(texColor.rgb, fogColor, fogFactor);
    FragColor = vec4(fogMixed, texColor.a);
}
