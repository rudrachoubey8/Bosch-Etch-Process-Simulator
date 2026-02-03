#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;

out vec3 FragPos;
out vec3 Normal;
out vec3 vColor;

uniform mat4 Projection;
uniform mat4 Size;
uniform mat4 Transform;
uniform mat4 Rotate;

void main()
{
    mat4 Model = Rotate * Transform * Size;

    vec4 worldPos = Model * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;

    mat3 NormalMatrix = transpose(inverse(mat3(Model)));
    Normal = normalize(NormalMatrix * aNormal);

    gl_Position = Projection * worldPos;

    vColor = aColor;
}
