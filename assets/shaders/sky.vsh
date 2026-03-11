

#version 330 core

out vec2 vUV;

void main()
{
    // Fullscreen triangle using gl_VertexID (no VBO needed)
    const vec2 verts[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );

    vec2 pos = verts[gl_VertexID];

    // Map from clip-space [-1,1] to UV [0,1]
    vUV = pos * 0.5 + 0.5;

    gl_Position = vec4(pos, 0.0, 1.0);
}