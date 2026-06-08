#version 430

const vec2 verts[6] = vec2[](
    vec2(-1,-1),
    vec2( 1,-1),
    vec2( 1, 1),

    vec2(-1,-1),
    vec2( 1, 1),
    vec2(-1, 1)
);
out vec2 uv;

void main()
{
    gl_Position = vec4(verts[gl_VertexID],0,1);
    uv = verts[gl_VertexID] * 0.5 + 0.5;
}