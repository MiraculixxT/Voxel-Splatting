#version 330 core

in VS_OUT {
    vec3 color;
    float opacity;
    vec2 localPos; // vom VS, ~[-1,1]^2
    vec2 uv;       // Texturkoordinate
    float layer;   // Atlas-Layer
} fs_in;

out vec4 fragColor;

// Block-Atlas wie im Mesh-Shader (Texture2DArray)
uniform sampler2DArray uBlockAtlas;

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

    fragColor = vec4(baseColor, alpha);
}