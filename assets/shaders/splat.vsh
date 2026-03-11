#version 330 core

// Quad-Ecke im lokalen 2D-Raum (-1..1)
layout(location = 0) in vec2 inCorner;

// Instanzdaten pro Splat
layout(location = 1) in vec3 inPosition; // center
layout(location = 2) in vec3 inScale;    // sigma in tangentialer Ebene
layout(location = 3) in vec3 inNormal;   // Face-Normal
layout(location = 4) in vec3 inColor;
layout(location = 5) in float inOpacity;

// Texturkoordinaten und Layer aus der Instanz
layout(location = 6) in vec2  inUV;
layout(location = 7) in float inLayer;

out VS_OUT {
    vec3 color;
    float opacity;
    vec2 localPos; // für Gaussian im Fragment
    vec2 uv;       // Texturkoordinate
    float layer;   // Atlas-Layer
    vec4 lightSpacePos; // Position im Licht-Raum für Shadow Mapping
} vs_out;

uniform mat4 uViewProj;
uniform mat4 lightViewProj;

void main()
{
    // Orthonormalbasis aus Normal
    vec3 n = normalize(inNormal);
    vec3 tangent;
    if (abs(n.y) > 0.5) {
        tangent = vec3(1.0, 0.0, 0.0);
    } else {
        tangent = vec3(0.0, 1.0, 0.0);
    }
    vec3 bitangent = normalize(cross(n, tangent));
    tangent        = normalize(cross(bitangent, n));

    // inCorner ∈ [-1,1]^2 → volles Quad nutzen
    vec2 p = inCorner;
    vec3 worldPos =
        inPosition +
        tangent   * (p.x * inScale.x) +
        bitangent * (p.y * inScale.y);

    // Position des Splat-Pixels im Licht-Raum (für Shadow Mapping)
    vs_out.lightSpacePos = lightViewProj * vec4(worldPos, 1.0);

    gl_Position = uViewProj * vec4(worldPos, 1.0);

    vs_out.color    = inColor;
    vs_out.opacity  = inOpacity;
    vs_out.localPos = p;        // für r^2 im Fragmentshader
    vs_out.uv       = inUV;     // Texturkoordinate weiterreichen
    vs_out.layer    = inLayer;  // Atlas-Layer weiterreichen
}