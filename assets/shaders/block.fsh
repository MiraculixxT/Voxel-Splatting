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
    float bias = max(0.0, 0.00003 * (1.0 - cosTheta));
    float contactOffset = -0.0005; // pull contact shadows slightly into the caster

    // 5x5 PCF sampling for smoother shadows
    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(uShadowMap, 0));
    for (int x = -2; x <= 2; ++x) {
        for (int y = -2; y <= 2; ++y) {
            float pcfDepth = texture(uShadowMap,
                                     projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += (currentDepth - contactOffset - bias > pcfDepth) ? 1.0 : 0.0;
        }
    }
    shadow /= 25.0;
    return shadow;
}

void main() {
    // Sample the 2D array
    int layer = int(TexCoord.z + 0.5);
    vec3 sampleCoord = TexCoord;
    if (layer == 6) { // water
        const float waveSpeed = 0.8;
        const float waveScale = 0.35;
        const float waveAmp = 0.03;
        float t = time * waveSpeed;
        float waveA = sin((WorldPos.x + t) * 2.3) + cos((WorldPos.z - t) * 1.9);
        float waveB = sin((WorldPos.x + WorldPos.z + t) * 1.3);
        vec2 offset = vec2(waveA, waveB) * waveAmp;
        sampleCoord.xy += offset * waveScale;
    }
    vec4 texColor = texture(textureArray, sampleCoord);
    float alphaCutoff = (layer == 9) ? 0.6 : 0.1; // 9 = leaves layer
    if (texColor.a < alphaCutoff) discard;
    if (layer == 9) {
        // Treat leaves as cutout-opaque to avoid fog/sky bleed on fringe pixels.
        texColor.a = 1.0;
    }

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
        // make fog grow a bit smoother and later
        fogFactor = fogFactor * fogFactor;
    }

    // Fog color: match the sky gradient along the view direction (same as in sky.fsh)
    vec3 viewDir = normalize(WorldPos - cameraPosition);
    float tSky = clamp(viewDir.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 horizonColor = vec3(0.5, 0.8, 1.0);
    vec3 zenithColor  = vec3(0.15, 0.35, 0.9);
    vec3 fogColor = mix(horizonColor, zenithColor, tSky);

    vec3 fogMixed = mix(litColor, fogColor, fogFactor);

    FragColor = vec4(fogMixed, texColor.a);


}