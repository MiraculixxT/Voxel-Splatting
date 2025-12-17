#version 410 core

in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D numberAtlas; // atlas with digits 0-9 laid out horizontally
uniform int digit;             // 0..9

void main()
{
    // Atlas is 10 digits wide horizontally, no padding
    float uPerDigit = 1.0 / 10.0;
    float u0 = clamp(float(digit), 0.0, 9.0) * uPerDigit;

    float u = u0 + TexCoord.x * uPerDigit;
    float v = TexCoord.y;

    vec4 sampleColor = texture(numberAtlas, vec2(u, v));

    // If the atlas uses black background with white digits, we can turn any non-black
    // pixel into a bright digit by checking luminance and tinting.
    float lum = dot(sampleColor.rgb, vec3(0.299, 0.587, 0.114));
    if (lum < 0.1)
        discard; // background

    // Tint digits to bright yellow so they pop over the bar
    FragColor = vec4(1.0, 1.0, 0.2, 1.0);
}
