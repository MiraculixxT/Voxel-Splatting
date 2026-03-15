#version 450 core
in vec2 v_uv;
in vec4 v_color;
out vec4 FragColor;

void main() {
    // Calculate the distance from the center of the quad
    float d2 = dot(v_uv, v_uv);

    // Gaussian falloff: G(x) = exp(-0.5 * d^2)
    float gaussian = exp(-0.5 * d2 * 4.0); // Multiply by 4.0 to make it fit the quad better

    float alpha = v_color.a * gaussian;

    // Discard very low alpha to save on blending cost
    if (alpha < 0.01) discard;

    // PRE-MULTIPLIED ALPHA: Multiply RGB by Alpha here
    // This prevents "dark edges" when many splats overlap
    FragColor = vec4(v_color.rgb * alpha, alpha);
}