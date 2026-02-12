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

uint hash(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

int voxelIndex(ivec3 c) {
    return c.x + c.y * gridSize.x + c.z * gridSize.x * gridSize.y;
}

void main() {
    uint id = gl_GlobalInvocationID.x;
    if (id >= particleCount) return;

    Particle p = particles[id];
    if (p.alive == 0) return;

    vec3 pos = vec3(p.x, p.y, p.z);
    vec3 dir = normalize(vec3(p.dx, p.dy, p.dz));
    float energy = p.energy;

    for (int step = 0; step < maxSteps; ++step) {
        ivec3 cell = ivec3(floor(pos / voxelSize));

        if (any(lessThan(cell, ivec3(0))) || any(greaterThanEqual(cell, gridSize)))
        {
            p.alive = 0;
           break;
        }

        int vidx = voxelIndex(cell);
        Voxel v = voxels[vidx];

        if (v.solid != 0) {
            float damage = energy * 0.3;

            uint writeIdx = atomicAdd(hitCount, 1u);
            if (writeIdx < MAX_HITS) {
                hits[writeIdx].cx = cell.x;
                hits[writeIdx].cy = cell.y;
                hits[writeIdx].cz = cell.z;
                hits[writeIdx].damage = damage;
                hits[writeIdx].flags = 0u;
            }

            if (v.threshold - damage <= 0.0){
                p.alive = 0;
                break;
            }

            uint h = hash(id ^ uint(step));
            if ((h & 255u) < 150u) {
                vec3 local = fract(pos / voxelSize);
                vec3 n;

                if (local.x < 0.01) n = vec3(-1,0,0);
                else if (local.x > 0.99) n = vec3(1,0,0);
                else if (local.y < 0.01) n = vec3(0,-1,0);
                else if (local.y > 0.99) n = vec3(0,1,0);
                else if (local.z < 0.01) n = vec3(0,0,-1);
                else n = vec3(0,0,1);

                dir = reflect(dir, n);
            } else {
                break;
            }

            energy -= damage;
            if (energy < 1e-6){
                p.alive = 0;
                break;
            }
        }

        pos += dir * voxelSize * 0.1;
    }
    if (p.alive != 0) {

        // update particle state before storing
        p.x = pos.x;
        p.y = pos.y;
        p.z = pos.z;
        p.dx = dir.x;
        p.dy = dir.y;
        p.dz = dir.z;
        p.energy = energy;

        uint idx = atomicAdd(finalParticlesCount, 1u);
        finalParticles[idx] = p;
    }

}
