#version 330 core
in vec3 FragPos;
in vec3 Normal;
in vec3 vColor;

out vec4 FragColor;

uniform vec3 viewPos;

void main()
{
    vec3 lightPos = vec3(0,0,2);

    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);

    float diff = max(dot(norm, lightDir), 0.0);

    vec3 baseColor = vColor;
    vec3 ambient = 0.5 * baseColor;
    vec3 diffuse = diff * baseColor;

    vec3 result = ambient + diffuse;
    FragColor = vec4(vColor, 1.0);
}