

#version 330 core

in vec2 vUV;
out vec4 FragColor;

uniform mat4 uInvViewProj;
uniform vec3 uCameraPos;
uniform vec3 uSunDir;

void main()
{
    // Reconstruct NDC from UV
    vec2 ndc = vUV * 2.0 - 1.0;

    // Assume a point on the far plane in clip space (z = 1, w = 1)
    vec4 clipPos = vec4(ndc, 1.0, 1.0);

    // Transform back to world space
    vec4 worldPos = uInvViewProj * clipPos;
    worldPos /= worldPos.w;

    vec3 dir = normalize(worldPos.xyz);

    // Simple sky gradient based on vertical direction
    float t = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 horizonColor = vec3(0.5, 0.8, 1.0);
    vec3 zenithColor  = vec3(0.15, 0.35, 0.9);
    vec3 skyColor = mix(horizonColor, zenithColor, t);

    // Sun in direction uSunDir
    float cosTheta = dot(normalize(dir), normalize(-uSunDir));

    // Angular radius of the sun (in radians)
    float sunRadius = radians(3.0);
    float inner = cos(sunRadius * 0.2);
    float outer = cos(sunRadius);

    // Core of the sun (disk)
    float sunFactor = smoothstep(outer, inner, cosTheta);
    // Slightly soften the edge
    sunFactor = pow(sunFactor, 0.7);

    // Wider halo around the sun
    float haloRadius = radians(8.0);
    float haloOuter = cos(haloRadius);
    float haloFactor = smoothstep(haloOuter, outer, cosTheta);

    // Very bright sun for future bloom
    vec3 sunColor = vec3(8.0, 7.5, 7.0);
    vec3 haloColor = vec3(1.2, 1.1, 1.0);

    vec3 color = skyColor + sunColor * sunFactor + haloColor * haloFactor;
    FragColor = vec4(color, 1.0);
}