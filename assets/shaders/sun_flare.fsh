#version 330 core

in vec2 vUV;
out vec4 FragColor;

uniform vec2 uSunScreenPos;    // sun position in screen UV (0..1)
uniform float uIntensity;      // 0..1, how strong the flare should be
uniform sampler2D uOcclusionTex; // occlusion mask (1 = sky, 0 = blocked)

// Smooth radial falloff (Gaussian-ish)
float radialGaussian(float d, float radius)
{
    float r = d / radius;
    return exp(-r * r);
}

// Soft circular blob
float softCircle(vec2 uv, vec2 center, float radius, float softness)
{
    float d = distance(uv, center);
    float inner = radius * (1.0 - softness);
    float outer = radius;
    return 1.0 - smoothstep(inner, outer, d);
}

// Simple ring mask (difference of two circles)
float softRing(vec2 uv, vec2 center, float innerRadius, float outerRadius, float softness)
{
    float outer = softCircle(uv, center, outerRadius, softness);
    float inner = softCircle(uv, center, innerRadius, softness);
    return clamp(outer - inner, 0.0, 1.0);
}

void main()
{
    vec2 center = vec2(0.5, 0.5);
    vec2 axis   = normalize(center - uSunScreenPos + 1e-5); // axis from sun toward center

    // Sample occlusion at the sun position. If the sun is fully behind geometry,
    // we skip drawing the flare.
    float occSun = texture(uOcclusionTex, uSunScreenPos).r;
    if (occSun <= 0.05) {
        FragColor = vec4(0.0);
        return;
    }

    // --- 1. Primary sun core and halo ---

    float d = distance(vUV, uSunScreenPos);

    // Tight, very bright core (small radius)
    float core = radialGaussian(d, 0.020);

    // Softer halo around it (slightly larger radius)
    float halo = radialGaussian(d, 0.080);

    // Subtle chromatic-ish edge by mixing a thin ring
    float ring = softRing(vUV, uSunScreenPos, 0.050, 0.070, 0.9);

    // --- 2. Ghost elements along center <-> sun axis ---

    // Positions entlang der Achse – relativ nah an der Sonne gehalten
    vec2 g1Pos = center + axis * 0.25;   // zwischen Center und Sonne
    vec2 g2Pos = center + axis * 0.55;   // näher an der Sonne
    vec2 g3Pos = center - axis * 0.35;   // Gegenseite

    float g1 = softCircle(vUV, g1Pos, 0.040, 0.9);
    float g2 = softCircle(vUV, g2Pos, 0.035, 0.9);
    float g3 = softCircle(vUV, g3Pos, 0.045, 0.9);

    // --- 3. Combine contributions ---

    // Base flare strength
    float flare =
        core * 3.0 +
        halo * 1.2 +
        ring * 1.0 +
        g1   * 0.9 +
        g2   * 0.7 +
        g3   * 0.6;

    flare *= uIntensity;

    // --- 4. Colouring ---

    // Bright, slightly warm light for core/halo
    vec3 sunColor  = vec3(3.5, 3.3, 3.1);

    // Slightly cooler/magenta-toned ghosts (very subtle)
    vec3 ghostCol1 = vec3(1.0, 1.1, 1.3);
    vec3 ghostCol2 = vec3(1.1, 0.95, 1.2);

    vec3 color =
        sunColor  * (core + halo) +
        sunColor  * ring * 0.8 +
        ghostCol1 * g1   * 0.8 +
        ghostCol1 * g2   * 0.7 +
        ghostCol2 * g3   * 0.7;

    // Modulate by flare strength and occlusion at the sun position
    color *= flare * occSun;

    // With GL_ONE, GL_ONE blending alpha is less important, but keep it sane
    float alpha = clamp(flare * occSun, 0.0, 1.0);
    FragColor = vec4(color, alpha);
}