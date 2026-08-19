#version 430

layout(local_size_x = 256) in;

#define MAX_HITS 50000u

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

struct Voxel
{
    float threshold;
    float depositThreshold;
    float voxelSize;

    int solid;
    int type;
};

struct HitEvent
{
    int cx;
    int cy;
    int cz;

    float damage;

    uint flags;

    int depositVoxelType;

    int nx;
    int ny;
    int nz;

    float incidenceCos;
};


layout(std430, binding = 5) readonly buffer ParticleBuffer
{
    Particle particles[];
};

layout(std430, binding = 6) readonly buffer VoxelBuffer
{
    Voxel voxels[];
};

layout(std430, binding = 7) buffer HitBuffer
{
    HitEvent hits[];
};

layout(std430, binding = 15) readonly buffer ReactionData
{
    float reactionData[];
};

layout(std430, binding = 8) buffer HitCounter
{
    uint hitCount;
};

layout(std430, binding = 9) buffer FinalParticlesCount
{
    uint finalParticlesCount;
};

layout(std430, binding = 10) buffer FinalParticles
{
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


float random01(uint seed)
{
    return float(hash(seed)) /
           4294967295.0;
}


int voxelIndex(ivec3 c)
{
    return c.x +
           c.y * gridSize.x +
           c.z * gridSize.x * gridSize.y;
}


bool inBounds(ivec3 c)
{
    return all(
        greaterThanEqual(
            c,
            ivec3(0)
        )
    )
    &&
    all(
        lessThan(
            c,
            gridSize
        )
    );
}


/*
 * Set up DDA traversal for the current particle position/direction.
 */
void initializeDDA(
    vec3 origin,
    vec3 dir,
    inout ivec3 cell,
    inout ivec3 stepDir,
    inout vec3 tMax,
    inout vec3 tDelta,
    inout vec3 invDir
)
{
    cell =
        ivec3(
            floor(origin / voxelSize)
        );

    for(int i = 0; i < 3; ++i)
    {
        if(abs(dir[i]) < 1e-8)
        {
            stepDir[i] = 0;
            invDir[i] = 0.0;
            tMax[i] = 3.402823e38;
            tDelta[i] = 3.402823e38;
            continue;
        }

        invDir[i] =
            1.0 / dir[i];

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
                voxelSize *
                abs(invDir[i]);
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
                voxelSize *
                abs(invDir[i]);
        }
    }
}


ivec3 getFaceNormal(
    ivec3 stepDir,
    vec3 tMax
)
{
    if(tMax.x < tMax.y)
    {
        if(tMax.x < tMax.z)
            return ivec3(-stepDir.x, 0, 0);

        return ivec3(0, 0, -stepDir.z);
    }

    if(tMax.y < tMax.z)
        return ivec3(0, -stepDir.y, 0);

    return ivec3(0, 0, -stepDir.z);
}


void main()
{
    uint id =
        gl_GlobalInvocationID.x;

    if(id >= uint(particleCount))
        return;


    Particle p =
        particles[id];


    if(p.alive == 0)
        return;


    vec3 origin =
        vec3(
            p.x,
            p.y,
            p.z
        );


    vec3 dir =
        normalize(
            vec3(
                p.dx,
                p.dy,
                p.dz
            )
        );


    if(length(dir) < 1e-8)
    {
        p.alive = 0;
        return;
    }


    ivec3 cell;
    ivec3 stepDir;

    vec3 tMax;
    vec3 tDelta;
    vec3 invDir;


    initializeDDA(
        origin,
        dir,
        cell,
        stepDir,
        tMax,
        tDelta,
        invDir
    );


    float t = 0.0;


    for(int step = 0;
        step < maxSteps;
        ++step)
    {
        /*
         * Particle has left the simulation.
         */

        if(
            any(
                lessThan(
                    cell,
                    ivec3(0)
                )
            )
            ||
            any(
                greaterThanEqual(
                    cell,
                    gridSize
                )
            )
            ||
            p.energy < 1.0
        )
        {
            p.alive = 0;
            break;
        }


        int vidx =
            voxelIndex(cell);


        Voxel v =
            voxels[vidx];


        /*
         * ========================================================
         * HIT
         * ========================================================
         */

        if(v.solid != 0)
        {
            ivec3 normal =
                getFaceNormal(
                    stepDir,
                    tMax
                );


            /*
             * Use independent random values for the different
             * physical outcomes.
             */

            uint baseSeed =
                hash(
                    id ^
                    uint(step) ^
                    uint(p.type * 747796405)
                );


            float rReaction =
                random01(
                    baseSeed ^
                    0x12345678u
                );


            float rDeposit =
                random01(
                    baseSeed ^
                    0x87654321u
                );


            float rAdsorb =
                random01(
                    baseSeed ^
                    0xDEADBEEFu
                );


            /*
             * Reaction table indexing:
             *
             * [particleType]
             * + [voxelType] * numberOfParticleTypes
             */

            int reactionIndex =
                p.type +
                v.type * typesOfParticles;


            int materialBlock =
                typesOfParticles *
                typesOfVoxels;


            float reactionChance =
                reactionData[
                    reactionIndex
                ];


            float depositChance =
                reactionData[
                    reactionIndex +
                    materialBlock
                ];


            float adsorbChance =
                reactionData[
                    reactionIndex +
                    materialBlock * 2
                ];


            reactionChance =
                clamp(
                    reactionChance,
                    0.0,
                    1.0
                );


            depositChance =
                clamp(
                    depositChance,
                    0.0,
                    1.0
                );


            adsorbChance =
                clamp(
                    adsorbChance,
                    0.0,
                    1.0
                );


            bool reflectParticle =
                rReaction <
                (1.0 - reactionChance);


            bool depositParticle =
                p.deposit != 0 &&
                rDeposit < depositChance;


            bool adsorbParticle =
                rAdsorb < adsorbChance;


            /*
             * ====================================================
             * ADSORPTION
             * ====================================================
             *
             * Particle is consumed.
             */

            if(adsorbParticle)
            {
                p.alive = 0;
                break;
            }


            /*
             * ====================================================
             * REFLECTION
             * ====================================================
             *
             * This is the ONLY normal hit case where the particle
             * survives.
             */

            if(
                reflectParticle &&
                !depositParticle
            )
            {
                /*
                 * Current position at the encountered boundary.
                 */

                vec3 hitPos =
                    origin +
                    dir * t;


                dir =
                    reflect(
                        dir,
                        normalize(
                            vec3(normal)
                        )
                    );


                /*
                 * Push the particle slightly away from the
                 * surface to prevent immediately hitting the
                 * exact same voxel again.
                 */

                origin =
                    hitPos +
                    normalize(
                        vec3(normal)
                    ) *
                    max(
                        voxelSize * 1e-3,
                        1e-5
                    );


                initializeDDA(
                    origin,
                    dir,
                    cell,
                    stepDir,
                    tMax,
                    tDelta,
                    invDir
                );


                /*
                 * Reflection energy loss.
                 *
                 * Change this according to your model.
                 */

                p.energy *= 0.8;


                t = 0.0;

                continue;
            }


            /*
             * ====================================================
             * NON-REFLECTED HIT
             * ====================================================
             */

            float incidenceCos =
                clamp(
                    abs(
                        dot(
                            normalize(-dir),
                            normalize(vec3(normal))
                        )
                    ),
                    0.0,
                    1.0
                );


            uint writeIdx =
                atomicAdd(
                    hitCount,
                    1u
                );


            /*
             * IMPORTANT:
             *
             * p.alive = 0 happens regardless of whether the hit
             * buffer has room.
             *
             * Otherwise a particle can continue propagating after
             * a hit when MAX_HITS has been exceeded.
             */

            if(writeIdx < MAX_HITS)
            {
                hits[writeIdx].cx =
                    cell.x;

                hits[writeIdx].cy =
                    cell.y;

                hits[writeIdx].cz =
                    cell.z;

                hits[writeIdx].damage =
                    max(
                        p.energy,
                        0.0
                    );


                if(depositParticle)
                {
                    hits[writeIdx].flags =
                        1u;

                    hits[writeIdx].depositVoxelType =
                        p.depositVoxelType;
                }
                else
                {
                    hits[writeIdx].flags =
                        0u;

                    hits[writeIdx].depositVoxelType =
                        0;
                }


                hits[writeIdx].nx =
                    normal.x;

                hits[writeIdx].ny =
                    normal.y;

                hits[writeIdx].nz =
                    normal.z;

                hits[writeIdx].incidenceCos =
                    incidenceCos;
            }


            /*
             * TERMINATE PARTICLE ON HIT.
             */

            p.alive = 0;

            break;
        }


        /*
         * ========================================================
         * ADVANCE DDA
         * ========================================================
         */

        if(tMax.x < tMax.y)
        {
            if(tMax.x < tMax.z)
            {
                t =
                    tMax.x;

                tMax.x +=
                    tDelta.x;

                cell.x +=
                    stepDir.x;
            }
            else
            {
                t =
                    tMax.z;

                tMax.z +=
                    tDelta.z;

                cell.z +=
                    stepDir.z;
            }
        }
        else
        {
            if(tMax.y < tMax.z)
            {
                t =
                    tMax.y;

                tMax.y +=
                    tDelta.y;

                cell.y +=
                    stepDir.y;
            }
            else
            {
                t =
                    tMax.z;

                tMax.z +=
                    tDelta.z;

                cell.z +=
                    stepDir.z;
            }
        }
    }


    /*
     * ============================================================
     * SAVE PARTICLE ONLY IF IT SURVIVED
     * ============================================================
     */

    if(p.alive != 0)
    {
        vec3 finalPos =
            origin +
            dir * t;


        p.x =
            finalPos.x;

        p.y =
            finalPos.y;

        p.z =
            finalPos.z;


        p.dx =
            dir.x;

        p.dy =
            dir.y;

        p.dz =
            dir.z;


        uint idx =
            atomicAdd(
                finalParticlesCount,
                1u
            );


        finalParticles[idx] =
            p;
    }
}
