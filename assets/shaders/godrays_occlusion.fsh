#version 330 core

in VS_OUT {
    vec2 TexCoord;
    float TexIndex;
} fs_in;

out vec4 FragColor;

uniform sampler2DArray textureArray;

void main()
{
    // Textur-Sample holen (gleiche TextureArray wie im Block-Shader)
    vec3 texCoord = vec3(fs_in.TexCoord, fs_in.TexIndex);
    vec4 texSample = texture(textureArray, texCoord);
    float alpha = texSample.a;

    // Opaque Pixel (z.B. Stamm, Stein, dichter Teil vom Blatt) blockieren Licht:
    //   occ = 0.0
    // Transparente / fast transparente Pixel lassen Licht durch:
    //   occ = 1.0
    float occ = (alpha > 0.1) ? 0.0 : 1.0;

    FragColor = vec4(occ, occ, occ, 1.0);
}