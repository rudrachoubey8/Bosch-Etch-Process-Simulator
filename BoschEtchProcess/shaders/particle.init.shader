#version 430

layout(local_size_x = 256) in;

struct Particle
{
    int alive;

    float x;
    float y;
    float z;

    float dx;
    float dy;
    float dz;

    int deposit;
    int depositVoxelType;

    float speed;
    float energy;

    int type;
};

struct EnergyBin
{
    float energy;
    float cdf;
};

layout(std430, binding = 5)
buffer ParticleBuffer
{
    Particle particles[];
};

layout(std430, binding = 9)
buffer FinalParticlesCount
{
    uint finalParticlesCount;
};

layout(std430, binding = 11)
readonly buffer IEDFBuffer
{
    EnergyBin bins[];
};

uniform uint startIndex;
uniform uint particleCount;

uniform int deposit;
uniform int depositVoxelType;
uniform int type;

uniform float cosTheta;
uniform float X;
uniform float Z;
uniform float seed;

uniform int nBins;
uniform float mass;

const float PI = 3.14159265359;
const float E_CHARGE = 1.602176634e-19;
const float AMU = 1.66053906660e-27;

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

float sampleIEDF(float r)
{
    int lo = 0;
    int hi = nBins - 1;

    while (lo < hi)
    {
        int mid = (lo + hi) / 2;

        if (r <= bins[mid].cdf)
            hi = mid;
        else
            lo = mid + 1;
    }

    return bins[lo].energy;
}

void main()
{
    uint id = gl_GlobalInvocationID.x;

    if (id >= particleCount)
        return;

    uint index = startIndex + id;

    //---------------------------------
    // Direction
    //---------------------------------

    float u =
        rand01(id + uint(seed));

    float v =
        rand01(id * 2u + uint(seed));

    float y =
        cosTheta +
        u * (1.0 - cosTheta);

    float phi =
        2.0 * PI * v;

    float r =
        sqrt(1.0 - y * y);

    float energy =
        sampleIEDF(
            rand01(
                id * 17u +
                uint(seed)));

    float M =
        mass * AMU;

    float speed =
        sqrt(
            2.0 *
            energy *
            E_CHARGE /
            M);

    particles[index].alive = 1;

    particles[index].deposit = deposit;
    particles[index].depositVoxelType = depositVoxelType;

    particles[index].type = type;

    particles[index].energy = energy;

    particles[index].speed = speed;

    particles[index].dx =
        r * cos(phi);

    particles[index].dy =
        -y;

    particles[index].dz =
        r * sin(phi);

    particles[index].x =
        rand01(id * 3u + uint(seed)) * X;

    particles[index].y = 290.0;

    particles[index].z =
        rand01(id * 4u + uint(seed)) * Z;

    if (id == 0)
    {
        atomicAdd(
            finalParticlesCount,
            particleCount);
    }
}
