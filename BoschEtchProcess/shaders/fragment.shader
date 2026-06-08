#version 430

in vec2 uv;

layout(binding = 4) uniform sampler2D screenTexture;

out vec4 FragColor;

void main()
{
    FragColor = texture(screenTexture, uv);
}