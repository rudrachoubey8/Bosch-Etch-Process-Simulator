#version 430
layout(local_size_x = 256) in;

#define MAX_HITS 500000u

struct Particle {
    int alive;
    float x,y,z;
    float dx, dy, dz;
    int deposit;
    int depositVoxelType;
    float speed;
    float energy;
    int type;
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
    int depositVoxelType;
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

layout(std430, binding = 15) buffer ReactionData {
    float reactionData[];
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
uniform int typesOfVoxels;
uniform int typesOfParticles;

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

    if(id >= particleCount)
        return;

    Particle p = particles[id];

    if(p.alive == 0)
        return;

    vec3 origin =
        vec3(p.x, p.y, p.z);

    vec3 dir =
        normalize(vec3(
            p.dx,
            p.dy,
            p.dz
        ));

    ivec3 cell =
        ivec3(floor(origin / voxelSize));

    ivec3 stepDir;

    vec3 tMax;
    vec3 tDelta;

    vec3 invDir = 1.0 / dir;

    for(int i = 0; i < 3; i++)
    {
        if(dir[i] > 0.0)
        {
            stepDir[i] = 1;

            float nextBoundary =
                (float(cell[i]) + 1.0)
                * voxelSize;

            tMax[i] =
                (nextBoundary - origin[i])
                * invDir[i];

            tDelta[i] =
                voxelSize * abs(invDir[i]);
        }
        else
        {
            stepDir[i] = -1;

            float nextBoundary =
                float(cell[i])
                * voxelSize;

            tMax[i] =
                (nextBoundary - origin[i])
                * invDir[i];

            tDelta[i] =
                voxelSize * abs(invDir[i]);
        }
    }

    float t = 0.0;

    for(int step = 0;
        step < maxSteps;
        step++)
    {
        if(any(lessThan(cell, ivec3(0))) ||
           any(greaterThanEqual(cell, gridSize)) || p.energy < 1)
        {
            p.alive = 0;
            break;
        }
        

        int vidx = voxelIndex(cell);

        Voxel v = voxels[vidx];

        if(v.solid != 0)
        {
            ivec3 normal;

            if(tMax.x < tMax.y)
            {
                if(tMax.x < tMax.z)
                    normal =
                        ivec3(-stepDir.x,0,0);
                else
                    normal =
                        ivec3(0,0,-stepDir.z);
            }
            else
            {
                if(tMax.y < tMax.z)
                    normal =
                        ivec3(0,-stepDir.y,0);
                else
                    normal =
                        ivec3(0,0,-stepDir.z);
            }

            uint h =
                hash(id ^ uint(step));

            float reactionChance =
                reactionData[
                    p.type +
                    v.type * typesOfParticles
                ];
            float depositChance =
                reactionData[
                    p.type +
                    v.type * typesOfParticles +
                    typesOfParticles * typesOfVoxels * 1
                ];
            float adsorbChance =
                reactionData[
                    p.type +
                    v.type * typesOfParticles +
                    typesOfParticles * typesOfVoxels * 2
                ];


            bool reflectParticle =
                (h / 4294967295.0f)
                < (1.0 - reactionChance);
            bool depositParticle =
                p.deposit != 0 &&
                (h / 4294967295.0f)
                < (depositChance);
            bool adsorbParticle = 
                (h / 4294967295.0f)
                < (adsorbChance);

            if(adsorbParticle) {
                p.alive = 0;
                break;
            }


            if(reflectParticle && !depositParticle)
            {
                vec3 hitPos =
                    origin + dir * t;

                dir =
                    reflect(dir,
                            vec3(normal));

                origin =
                    hitPos +
                    vec3(normal) * 0.01;

                cell =
                    ivec3(
                        floor(origin / voxelSize)
                    );

                invDir = 1.0 / dir;

                for(int i = 0; i < 3; i++)
                {
                    if(dir[i] > 0.0)
                    {
                        stepDir[i] = 1;

                        float nextBoundary =
                            (float(cell[i]) + 1.0)
                            * voxelSize;

                        tMax[i] =
                            (nextBoundary
                             - origin[i])
                            * invDir[i];

                        tDelta[i] =
                            voxelSize
                            * abs(invDir[i]);
                    }
                    else
                    {
                        stepDir[i] = -1;

                        float nextBoundary =
                            float(cell[i])
                            * voxelSize;

                        tMax[i] =
                            (nextBoundary
                             - origin[i])
                            * invDir[i];

                        tDelta[i] =
                            voxelSize
                            * abs(invDir[i]);
                    }
                }
                p.energy *= 0.8; // To edit
                t = 0.0;
                continue;
            }

            float damage = p.energy;

            uint writeIdx =
                atomicAdd(hitCount, 1u);

            if(writeIdx < MAX_HITS)
            {
                if(!depositParticle)
                {
                    hits[writeIdx].cx =
                        cell.x;

                    hits[writeIdx].cy =
                        cell.y;

                    hits[writeIdx].cz =
                        cell.z;

                    hits[writeIdx].damage =
                        damage;

                    hits[writeIdx].flags = 0u;
                    hits[writeIdx].depositVoxelType = 0;
                }
                else
                {
                    ivec3 neighbor =
                        cell + normal;

                    hits[writeIdx].cx =
                        cell.x;

                    hits[writeIdx].cy =
                        cell.y;

                    hits[writeIdx].cz =
                        cell.z;

                    hits[writeIdx].damage =
                        damage;

                    hits[writeIdx].flags = 1u;
                    hits[writeIdx].depositVoxelType = p.depositVoxelType;
                }
            }

            p.alive = 0;
            break;
        }

        if(tMax.x < tMax.y)
        {
            if(tMax.x < tMax.z)
            {
                t = tMax.x;

                tMax.x += tDelta.x;

                cell.x += stepDir.x;
            }
            else
            {
                t = tMax.z;

                tMax.z += tDelta.z;

                cell.z += stepDir.z;
            }
        }
        else
        {
            if(tMax.y < tMax.z)
            {
                t = tMax.y;

                tMax.y += tDelta.y;

                cell.y += stepDir.y;
            }
            else
            {
                t = tMax.z;

                tMax.z += tDelta.z;

                cell.z += stepDir.z;
            }
        }
    }

    if(p.alive != 0)
    {
        vec3 finalPos =
            origin + dir * t;

        p.x = finalPos.x;
        p.y = finalPos.y;
        p.z = finalPos.z;

        p.dx = dir.x;
        p.dy = dir.y;
        p.dz = dir.z;

        uint idx =
            atomicAdd(
                finalParticlesCount,
                1u
            );

        finalParticles[idx] = p;
    }
}
