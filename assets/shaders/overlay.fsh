#version 410 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D screenTexture;
uniform vec3 color;
uniform int useTexture;

void main()
{
    if(useTexture == 1)
        FragColor = texture(screenTexture, TexCoords);
    else
        FragColor = vec4(color, 1.0);
}
