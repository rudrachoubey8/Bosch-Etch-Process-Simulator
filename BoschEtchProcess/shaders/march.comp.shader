#version 430
layout(local_size_x = 256) in;

#define MAX_HITS 500000u

struct Particle {
    int alive;
    float x,y,z;
    float dx, dy, dz;
    int deposit;
    float speed;
    float energy;
};

struct Voxel {
    float threshold;
    float depositThreshold;
    float voxelSize;
    int solid;
    int type;
};

struct HitEvent {
    int cx, cy, cz;
    float damage;
    uint flags;
};

layout(std430, binding = 5) readonly buffer ParticleBuffer {
    Particle particles[];
};

layout(std430, binding = 6) readonly buffer VoxelBuffer {
    Voxel voxels[];
};

layout(std430, binding = 7) buffer HitBuffer {
    HitEvent hits[];
};

layout(std430, binding = 8) buffer HitCounter {
    uint hitCount;
};

layout(std430, binding = 9) buffer FinalParticlesCount {
    uint finalParticlesCount;
};

layout(std430, binding = 10) buffer FinalParticles {
    Particle finalParticles[];
};

uniform float voxelSize;
uniform int maxSteps;
uniform ivec3 gridSize;
uniform int particleCount;

uint hash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

int voxelIndex(ivec3 c)
{
    return c.x + c.y * gridSize.x + c.z * gridSize.x * gridSize.y;
}

void main()
{
    uint id = gl_GlobalInvocationID.x;
    if(id >= particleCount) return;

    Particle p = particles[id];
    if(p.alive == 0) return;

    vec3 pos = vec3(p.x, p.y, p.z);
    vec3 dir = normalize(vec3(p.dx, p.dy, p.dz));
    float energy = p.energy;

    ivec3 cell = ivec3(floor(pos / voxelSize));

    vec3 rayDir = dir;
    vec3 invDir = 1.0 / rayDir;

    vec3 voxelPos = vec3(cell) * voxelSize;

    vec3 tMax;
    vec3 tDelta;
    ivec3 stepDir;

    for(int i = 0; i < 3; i++)
    {
        if(rayDir[i] > 0.0)
        {
            stepDir[i] = 1;
            float nextBoundary = voxelPos[i] + voxelSize;
            tMax[i] = (nextBoundary - pos[i]) * invDir[i];
            tDelta[i] = voxelSize * invDir[i];
        }
        else
        {
            stepDir[i] = -1;
            float nextBoundary = voxelPos[i];
            tMax[i] = (nextBoundary - pos[i]) * invDir[i];
            tDelta[i] = -voxelSize * invDir[i];
        }
    }

    for(int step = 0; step < maxSteps; step++)
    {
        if(any(lessThan(cell, ivec3(0))) ||
           any(greaterThanEqual(cell, gridSize)))
        {
            p.alive = 0;
            break;
        }

        int vidx = voxelIndex(cell);
        Voxel v = voxels[vidx];

        if(v.solid != 0)
        {
            ivec3 normal;

            if(tMax.x < tMax.y && tMax.x < tMax.z)
                normal = ivec3(-stepDir.x,0,0);
            else if(tMax.y < tMax.z)
                normal = ivec3(0,-stepDir.y,0);
            else
                normal = ivec3(0,0,-stepDir.z);

            float damage = 10.0;

            uint writeIdx = atomicAdd(hitCount,1u);

            if(writeIdx < MAX_HITS)
            {
                if(p.deposit == 0)
                {
                    hits[writeIdx].cx = cell.x;
                    hits[writeIdx].cy = cell.y;
                    hits[writeIdx].cz = cell.z;
                    hits[writeIdx].damage = damage;
                    hits[writeIdx].flags = 0u;
                }
                else
                {
                    ivec3 neighbor = cell + normal;

                    hits[writeIdx].cx = neighbor.x;
                    hits[writeIdx].cy = neighbor.y;
                    hits[writeIdx].cz = neighbor.z;
                    hits[writeIdx].damage = damage;
                    hits[writeIdx].flags = 1u;

                    p.alive = 0;
                    break;
                }
            }

            if(v.threshold - damage <= 0.0)
            {
                p.alive = 0;
                break;
            }

            uint h = hash(id ^ uint(step));

            if((h & 255u) < 150u)
            {
                dir = reflect(dir, vec3(normal));
                rayDir = dir;
                invDir = 1.0 / rayDir;
            }
            else
            {
                break;
            }

            energy -= damage;

            if(energy < 1e-6)
            {
                p.alive = 0;
                break;
            }
        }

        if(tMax.x < tMax.y)
        {
            if(tMax.x < tMax.z)
            {
                cell.x += stepDir.x;
                pos += rayDir * tMax.x;
                tMax.x += tDelta.x;
            }
            else
            {
                cell.z += stepDir.z;
                pos += rayDir * tMax.z;
                tMax.z += tDelta.z;
            }
        }
        else
        {
            if(tMax.y < tMax.z)
            {
                cell.y += stepDir.y;
                pos += rayDir * tMax.y;
                tMax.y += tDelta.y;
            }
            else
            {
                cell.z += stepDir.z;
                pos += rayDir * tMax.z;
                tMax.z += tDelta.z;
            }
        }
    }

    if(p.alive != 0)
    {
        p.x = pos.x;
        p.y = pos.y;
        p.z = pos.z;

        p.dx = dir.x;
        p.dy = dir.y;
        p.dz = dir.z;

        p.energy = energy;

        uint idx = atomicAdd(finalParticlesCount,1u);
        finalParticles[idx] = p;
    }
}