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
uniform int type;


uniform float cosTheta;
uniform float X;
uniform float Z;
uniform float seed;

uint hash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float rand01(uint x)
{
    return float(hash(x)) / 4294967296.0;
}

void main()
{
    uint id = gl_GlobalInvocationID.x;

    if(id >= particleCount)
        return;

    uint index = startIndex + id;

    float u = rand01(id + uint(seed));
    float v = rand01(id * 2 + uint(seed));

    float y = cosTheta + u * (1.0 - cosTheta);
    float phi = 6.28318530718 * v;
    float r = sqrt(1.0 - y * y);

    particles[index].deposit = type;
    particles[index].speed = 10.0;
    particles[index].alive = 1;
    particles[index].energy = 50.0;

    particles[index].dx = r * cos(phi);
    particles[index].dy = y;
    particles[index].dz = r * sin(phi);

    particles[index].x = X / 3 + rand01(id * 3 + uint(seed)) * X / 3;
    particles[index].y = 1.0;
    particles[index].z = Z / 3 + rand01(id * 4 + uint(seed)) * Z / 3;
    if(id == 0)
    {
        atomicAdd(finalParticlesCount, particleCount);
    }
}