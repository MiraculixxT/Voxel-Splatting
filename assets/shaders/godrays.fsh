#version 330 core

in vec2 vUV;
out vec4 FragColor;

uniform vec2 uSunScreenPos;     // sun pos in screen space
uniform float uIntensity;        // overall intensity
uniform sampler2D uOcclusionTex; // occlusion mask (1 = sky, 0 = blocked)

// Radial sampling towards the sun + occlusion
void main()
{
    vec2 toSun = uSunScreenPos - vUV;
    float distToSun = length(toSun);

    if (distToSun < 1e-4) {
        float occ = texture(uOcclusionTex, vUV).r;
        vec3 coreColor = vec3(1.4, 1.35, 1.3) * uIntensity * occ * 1.5;
        FragColor = vec4(coreColor, uIntensity * occ);
        return;
    }

    vec2 dir = toSun / distToSun;

    const int NUM_SAMPLES = 48;
    float stepSize = distToSun / float(NUM_SAMPLES);

    float accum = 0.0;
    float weightSum = 0.0;

    vec2 p = vUV;

    for (int i = 0; i < NUM_SAMPLES; ++i) {
        p += dir * stepSize;
        float occ = texture(uOcclusionTex, p).r;

        float t = float(i) / float(NUM_SAMPLES);
        float w = (1.0 - t) * exp(-t * 2.2);

        accum += w * occ;
        weightSum += w;
    }

    float ray = (weightSum > 0.0) ? accum / weightSum : 0.0;

    float distFalloff = exp(-distToSun * 4.5);
    ray *= distFalloff;

    ray *= uIntensity;

    vec3 rayColor = vec3(1.25, 1.2, 1.15) * ray;

    FragColor = vec4(rayColor, ray);
}