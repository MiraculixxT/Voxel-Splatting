#version 330 core

in VS_OUT {
    vec3 color;
    float opacity;
    vec2 localPos;   // vom VS, ~[-1,1]^2
    vec2 uv;         // Texturkoordinate
    float layer;     // Atlas-Layer
    vec4 lightSpacePos; // Position im Licht-Raum für Shadow Mapping
} fs_in;

out vec4 fragColor;

// Block-Atlas wie im Mesh-Shader (Texture2DArray)
uniform sampler2DArray uBlockAtlas;
uniform sampler2D uShadowMap;
uniform vec3 lightDir;           // gleiche Richtung wie im Block-Shader
uniform vec3 sunColor = vec3(1.0, 0.96, 0.88); // warme Sonnenfarbe

float calcShadow(vec4 lightSpacePos)
{
    // Clip-Space -> [0,1] Texture-Koordinaten
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords = projCoords * 0.5 + 0.5;

    // Außerhalb der ShadowMap? Dann kein Schatten.
    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z > 1.0)
    {
        return 0.0;
    }

    float currentDepth = projCoords.z;
    float bias = 0.002;

    // 3x3 PCF
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

void main()
{
    // 1) Texturfarbe aus dem Atlas holen
    vec4 tex = texture(uBlockAtlas, vec3(fs_in.uv, fs_in.layer));
    vec3 baseColor = tex.rgb;

    // Optional: mit Splat-Farbe modulieren (für Stilvariation)
    // baseColor *= fs_in.color;

    // 2) Gaussian-Falloff in lokalen Quad-Koordinaten
    vec2 uvLocal = fs_in.localPos;   // ~[-1,1]^2
    float r2 = dot(uvLocal, uvLocal);

    // engerer Gaussian, damit nur der Kern sichtbar ist
    float w = exp(-r2 * 3.5);

    // schwache Ränder wegwerfen, um harte Ecken zu vermeiden
    if (w < 0.3)
        discard;

    // Alpha kombiniert aus Gaussian, Instanz-Opacity und Texturalpha
    float alpha = w * fs_in.opacity * tex.a;

    // 3) Schattenfaktor aus ShadowMap
    float shadow = calcShadow(fs_in.lightSpacePos);

    // Schatten etwas abschwächen, damit Splats nicht komplett absaufen
    const float shadowStrength = 0.6;
    shadow *= shadowStrength;

    // Einfache Beleuchtung: warmes Sonnenlicht + Schatten
    vec3 litColor = baseColor * sunColor * (1.0 - shadow);

    fragColor = vec4(litColor, alpha);
}