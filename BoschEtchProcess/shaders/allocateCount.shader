#version 430
layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

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
    
    uint vertexOffset;
    uint vertexCount;
    int generatedCount;
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

layout(std430, binding = 13) buffer DirtyCounter {
    uint dirtyCount;
};
layout(std430, binding = 3) buffer DirtyIndices {
    uint dirtyIndices[];
};

uniform ivec3 gridSize;
uniform ivec3 chunkGridSize;
uniform int dirtyCount;

bool inBounds(int x,int y,int z)
{
    return
        x >= 0 && y >= 0 && z >= 0 &&
        x < gridSize.x &&
        y < gridSize.y &&
        z < gridSize.z;
}
bool solidAt(int x,int y,int z)
{
    if(!inBounds(x,y,z))
        return false;

    return voxels[getVoxel(ivec3(x,y,z))].solid != 0;
}


void main(){
    
    uint chunkDispatch =
        gl_WorkGroupID.x / 4u;

    uint chunkLocalX =
        gl_WorkGroupID.x % 4u;

    if(chunkDispatch >= dirtyCount)
        return;

    uint chunkIndex =
        dirtyIndices[chunkDispatch];

    ivec3 chunkOrigin =
        ivec3(
            chunks[chunkIndex].chunkX,
            chunks[chunkIndex].chunkY,
            chunks[chunkIndex].chunkZ
        ) * CHUNK_SIZE;

    ivec3 subBlock =
        ivec3(
            int(chunkLocalX),
            int(gl_WorkGroupID.y),
            int(gl_WorkGroupID.z)
        ) * 8;

    ivec3 p =
        chunkOrigin +
        subBlock +
        ivec3(gl_LocalInvocationID);

    if(!inBounds(p.x,p.y,p.z))
        return;

    if(!solidAt(p.x,p.y,p.z))
        return;

    atomicAdd(voxelCount, 1u);

    bool px = !solidAt(p.x + 1, p.y, p.z);
    bool nx = !solidAt(p.x - 1, p.y, p.z);

    bool py = !solidAt(p.x, p.y + 1, p.z);
    bool ny = !solidAt(p.x, p.y - 1, p.z);

    bool pz = !solidAt(p.x, p.y, p.z + 1);
    bool nz = !solidAt(p.x, p.y, p.z - 1);

    if(!(px || nx || py || ny || pz || nz))
        return;

    uint faceCount = 0u;

    faceCount += uint(px);
    faceCount += uint(nx);
    faceCount += uint(py);
    faceCount += uint(ny);
    faceCount += uint(pz);
    faceCount += uint(nz);

    atomicAdd(chunks[chunkIndex].vertexCount, faceCount * 6u);

    
}