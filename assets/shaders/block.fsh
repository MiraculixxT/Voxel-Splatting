#version 410 core
out vec4 FragColor;

// (u, v, layer)
in vec3 TexCoord;
in vec3 WorldPos;
in vec4 LightSpacePos; // from vertex shader

// Use a 2D Array Sampler
uniform sampler2DArray textureArray;
uniform vec3 cameraPosition;
uniform float time;
uniform float fogStart;
uniform float fogEnd;

uniform sampler2D uShadowMap;   // shadow map
uniform vec3 lightDir = vec3(-0.3, -1.0, -0.2); // directional light
uniform vec3 sunColor = vec3(1.0, 0.96, 0.88); // warm sunlight tint



float calcShadow(vec4 lightSpacePos, vec3 normal) {
    // Project from clip space to [0,1] texture space
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords = projCoords * 0.5 + 0.5;

    // Outside the shadow map
    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z > 1.0)
    {
        return 0.0;
    }

    float currentDepth = projCoords.z;

    // Slope-dependent bias: smaller on front faces, larger on grazing angles
    vec3 L = normalize(-lightDir);
    float cosTheta = max(dot(normalize(normal), L), 0.0);
    float bias = max(0.0005, 0.003 * (1.0 - cosTheta));

    // 3x3 PCF sampling
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(uShadowMap, 0);
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(uShadowMap,
                                     projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
        }
    }
    shadow /= 9.0;
    return shadow;
}

void main() {
    // Sample the 2D array
    vec4 texColor = texture(textureArray, TexCoord);
    if (texColor.a < 0.1) discard;

    // Compute geometric normal from world-space position (works for block faces)
    vec3 dx = dFdx(WorldPos);
    vec3 dy = dFdy(WorldPos);
    vec3 normal = normalize(cross(dx, dy));

    // Shadow factor in [0,1] (1 = fully shadowed)
    float shadow = calcShadow(LightSpacePos, normal);

    // Reduce overall shadow strength so everything is less dark
    const float shadowStrength = 0.6;
    shadow *= shadowStrength;

    vec3 L = normalize(-lightDir);
    float NdotL = max(dot(normal, L), 0.0);

    // Ambient + diffuse lighting model with warm sunlight tint
    vec3 ambient = 0.45 * texColor.rgb * sunColor;
    vec3 diffuse = (1.0 - shadow) * NdotL * texColor.rgb * sunColor;
    vec3 litColor = ambient + diffuse;

    // FOG
    float dist = distance(WorldPos, cameraPosition);
    float fogFactor = 0.0;
    if (fogEnd > fogStart) {
        fogFactor = clamp((dist - fogStart) / (fogEnd - fogStart), 0.0, 1.0);
    }
    vec3 fogColor = vec3(0.6f, 0.8f, 0.95f); // slightly warmer sky color
    vec3 fogMixed = mix(litColor, fogColor, fogFactor);

    FragColor = vec4(fogMixed, texColor.a);


}