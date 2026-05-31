#version 430

layout(local_size_x=8, local_size_y=8, local_size_z=8) in;

struct Voxel
{
    float threshold;
    float depositThreshold;
    float voxelSize;
    int solid;
    int type;
};

struct Vertex
{
    vec3 pos;
    vec3 normal;
    vec3 color;
};
#define CHUNK_SIZE 32
#define CHUNK_MASK 31
#define CHUNK_VOLUME 32768

struct Chunk
{
    int chunkX;
    int chunkY;
    int chunkZ;
    int dirty;
    
    uint vertexOffset;
    uint vertexCount;
};


layout(std430, binding = 6) buffer ChunkBuffer
{
    Chunk chunks[];
};
layout(std430, binding = 12) buffer VoxelBuffer {
    Voxel voxels[];
};

layout(std430, binding = 1) writeonly buffer Vertices
{
    Vertex verts[];
};

layout(std430, binding = 14) buffer VoxelCounter
{
    uint voxelCount;
};

layout(std430, binding = 2) buffer Counter
{
    uint vertCount;
};
layout(std430, binding = 3) buffer DirtyIndices
{
    uint dirtyIndices[];
};
uniform ivec3 gridSize;
uniform ivec3 chunkGridSize;
uniform int dirtyCount;

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
int typeAt(int x,int y,int z)
{
    if(!inBounds(x,y,z))
        return -1;

    return voxels[getVoxel(ivec3(x,y,z))].type;
}

vec3 colorFromType(int t)
{
    if(t == 0) return vec3(1.0,0.647,0.0);
    if(t == 1) return vec3(0.0,1.0,0.0);
    if(t == 2) return vec3(0.5,0.0,0.5);
    if(t == 3) return vec3(0.0,1.0,1.0);

    return vec3(1.0);
}

void writeFace(
    uint i,
    vec3 base,
    vec3 du,
    vec3 dv,
    vec3 normal,
    vec3 color
)
{
    bool flip =
        normal.x < 0.0 ||
        normal.y < 0.0 ||
        normal.z < 0.0;

    if(!flip)
    {
        verts[i+0] = Vertex(base,           normal, color);
        verts[i+1] = Vertex(base + du,      normal, color);
        verts[i+2] = Vertex(base + du + dv, normal, color);

        verts[i+3] = Vertex(base,           normal, color);
        verts[i+4] = Vertex(base + du + dv, normal, color);
        verts[i+5] = Vertex(base + dv,      normal, color);
    }
    else
    {
        verts[i+0] = Vertex(base,           normal, color);
        verts[i+1] = Vertex(base + du + dv, normal, color);
        verts[i+2] = Vertex(base + du,      normal, color);

        verts[i+3] = Vertex(base,           normal, color);
        verts[i+4] = Vertex(base + dv,      normal, color);
        verts[i+5] = Vertex(base + du + dv, normal, color);
    }
}

void main()
{
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

    uint v = chunks[chunkIndex].vertexOffset + atomicAdd(chunks[chunkIndex].vertexCount, faceCount * 6u);

    vec3 color = colorFromType(typeAt(p.x,p.y,p.z));

    vec3 pos = vec3(p);

    // +X
    if(px)
    {
        writeFace(
            v,
            pos + vec3(1,0,0),
            vec3(0,1,0),
            vec3(0,0,1),
            vec3(1,0,0),
            color
        );
        v += 6;
    }

    // -X
    if(nx)
    {
        writeFace(
            v,
            pos,
            vec3(0,1,0),
            vec3(0,0,1),
            vec3(-1,0,0),
            color
        );
        v += 6;
    }

    // +Y
    if(py)
    {
        writeFace(
            v,
            pos + vec3(0,1,0),
            vec3(1,0,0),
            vec3(0,0,1),
            vec3(0,1,0),
            color
        );
        v += 6;
    }

    // -Y
    if(ny)
    {
        writeFace(
            v,
            pos,
            vec3(1,0,0),
            vec3(0,0,1),
            vec3(0,-1,0),
            color
        );
        v += 6;
    }

    // +Z
    if(pz)
    {
        writeFace(
            v,
            pos + vec3(0,0,1),
            vec3(1,0,0),
            vec3(0,1,0),
            vec3(0,0,1),
            color
        );
        v += 6;
    }

    // -Z
    if(nz)
    {
        writeFace(
            v,
            pos,
            vec3(1,0,0),
            vec3(0,1,0),
            vec3(0,0,-1),
            color
        );
        v += 6;
    }
}