#version 430
layout(local_size_x = 256) in;

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

layout(std430, binding = 6) buffer VoxelBuffer {
    Voxel voxels[];
};

layout(std430, binding = 7) readonly buffer HitBuffer {
    HitEvent hits[];
};

layout(std430, binding = 8) readonly buffer HitCounter {
    uint hitCount;
};

uniform ivec3 gridSize;

int voxelIndex(ivec3 c)
{
    return c.x + c.y * gridSize.x + c.z * gridSize.x * gridSize.y;
}

void main()
{
    uint id = gl_GlobalInvocationID.x;
    if (id >= hitCount) return;

    HitEvent h = hits[id];

    ivec3 cell = ivec3(h.cx, h.cy, h.cz);

    if(any(lessThan(cell, ivec3(0))) ||
       any(greaterThanEqual(cell, gridSize)))
        return;

    int vidx = voxelIndex(cell);

    Voxel v = voxels[vidx];

    if(v.solid == 0) return;

    if((h.flags & 1u) == 1u)
    {
        v.depositThreshold -= h.damage;

        if(v.depositThreshold <= 0.0)
        {
            v.solid = 1;
            v.type = 2;
            v.threshold = 1000;
            v.depositThreshold = 10;
        }
    }
    else
    {
        v.threshold -= h.damage;

        if(v.threshold <= 0.0)
        {
            v.solid = 0;
            v.type = 0;
        }
    }

    voxels[vidx] = v;
}