#version 330 core
in vec3 FragPos;
in vec3 Normal;
in vec3 vColor;

out vec4 FragColor;

uniform vec3 lightPos;   // (0,0,2)
uniform vec3 viewPos;

void main()
{
    vec3 norm = normalize(Normal);

    vec3 lightDir = normalize(-lightPos + FragPos);
    float diff = abs(dot(norm, lightDir));

    vec3 baseColor = vec3(0.8, 0.2, 0.2);
    vec3 ambient = 0.1 * baseColor;
    vec3 diffuse = diff * baseColor;

    vec3 result = ambient + diffuse;
    FragColor = vec4(vColor, 1.0);
}
