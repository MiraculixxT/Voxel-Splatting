#version 410 core
out vec4 FragColor;

// (u, v, layer)
in vec3 TexCoord;

// Use a 2D Array Sampler
uniform sampler2DArray textureArray;

void main() {
    // Sample the 2D array
    vec4 texColor = texture(textureArray, TexCoord);
    if(texColor.a < 0.1)
    discard;
    FragColor = texColor;
}