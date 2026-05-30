#version 430
layout(local_size_x = 256) in;

#define CHUNK_SIZE 32
#define CHUNK_MASK 31
#define CHUNK_VOLUME 32768

struct Voxel {
    float threshold;
    float depositThreshold;
    float voxelSize;
    int solid;
    int type;
};

struct Chunk {
    int chunkX;
    int chunkY;
    int chunkZ;
    int dirty;

};

struct HitEvent {
    int cx, cy, cz;
    float damage;
    uint flags;
};

layout(std430, binding = 6) buffer ChunkBuffer {
    Chunk chunks[];
};
layout(std430, binding = 12) buffer VoxelBuffer {
    Voxel voxels[];
};

layout(std430, binding = 7) readonly buffer HitBuffer {
    HitEvent hits[];
};

layout(std430, binding = 8) readonly buffer HitCounter {
    uint hitCount;
};

uniform ivec3 gridSize;
uniform ivec3 chunkGridSize;

int getChunkIndex(ivec3 worldPos)
{
    ivec3 chunkCoord = worldPos >> 5;

    return
          chunkCoord.x
        + chunkCoord.y * chunkGridSize.x
        + chunkCoord.z * chunkGridSize.x * chunkGridSize.y;
}

int getVoxel(ivec3 worldPos)
{
    return worldPos.x + worldPos.y * gridSize.x + worldPos.z * gridSize.x * gridSize.y;
}

void main()
{
    uint id = gl_GlobalInvocationID.x;

    if(id >= hitCount)
        return;

    HitEvent h = hits[id];

    ivec3 cell =
        ivec3(
            h.cx,
            h.cy,
            h.cz
        );

    if(any(lessThan(cell, ivec3(0))) ||
       any(greaterThanEqual(cell, gridSize)))
    {
        return;
    }

    int chunkIndex =
        getChunkIndex(cell);

    int index = getVoxel(cell);
    Voxel v = voxels[index];

    bool changed = false;

    if((h.flags & 1u) == 1u)
    {
        // deposition

        if(v.solid == 0)
        {
            v.depositThreshold -= h.damage;

            if(v.depositThreshold <= 0.0)
            {
                v.solid = 1;
                v.type = 2;
                v.threshold = 1000.0;
                v.depositThreshold = 10.0;

                changed = true;
            }

            voxels[index] = v;
        }
    }
    else
    {
        // erosion

        if(v.solid == 0)
            return;

        v.threshold -= h.damage;

        if(v.threshold <= 0.0)
        {
            v.solid = 0;
            v.type = 0;

            changed = true;
        }

        voxels[index] = v;
    }

    if(changed)
    {
        atomicExchange(
            chunks[chunkIndex].dirty,
            1
        );
    }
}