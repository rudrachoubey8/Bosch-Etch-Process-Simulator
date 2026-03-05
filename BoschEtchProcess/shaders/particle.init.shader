#version 430
layout(local_size_x = 256) in;

struct Particle {
    int alive;
    float x,y,z;
    float dx, dy, dz;
    int deposit;
    float speed;
    float energy;
};

layout(std430, binding = 5) buffer ParticleBuffer {
    Particle particles[];
};

layout(std430, binding = 9) buffer FinalParticlesCount {
    uint finalParticlesCount;
};


uniform uint startIndex;
uniform uint particleCount;

uniform float cosTheta;
uniform float X;
uniform float Z;
uniform float seed;

float rand(float n)
{
    return fract(sin(n) * 43758.5453123);
}
void main()
{
    uint id = gl_GlobalInvocationID.x;

    if(id >= particleCount)
        return;

    uint index = startIndex + id;

    float u = rand(id + seed);
    float v = rand(id * 2.0 + seed);

    float y = cosTheta + u * (1.0 - cosTheta);
    float phi = 6.28318530718 * v;
    float r = sqrt(1.0 - y * y);

    particles[index].deposit = 0;
    particles[index].speed = 10.0;
    particles[index].alive = 1;
    particles[index].energy = 50.0;

    particles[index].dx = r * cos(phi);
    particles[index].dy = y;
    particles[index].dz = r * sin(phi);

    particles[index].x = rand(id * 3.0 + seed) * X;
    particles[index].y = 1.0;
    particles[index].z = rand(id * 4.0 + seed) * Z;
    if(id == 0)
    {
        atomicAdd(finalParticlesCount, particleCount);
    }
}