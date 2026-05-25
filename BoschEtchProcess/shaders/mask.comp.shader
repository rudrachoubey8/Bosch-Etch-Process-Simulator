#version 430

layout(local_size_x=8, local_size_y=8, local_size_z=8) in;

struct Voxel {
    float threshold;
    float depositThreshold;
    float voxelSize;
    int solid;
    int type;
};

struct Vertex {
    vec3 pos;
    vec3 normal;
    vec3 color;
};

layout(std430, binding = 6) readonly buffer Voxels {
    Voxel voxels[];
};

layout(std430, binding = 1) writeonly buffer Vertices {
    Vertex verts[];
};

layout(std430, binding = 13) buffer Counter {
    uint mask[];
};

uniform ivec3 gridSize;
uniform int direction;

int idx(ivec3 p)
{
    if(p.x < 0 || p.y < 0 || p.z < 0) return -1;

    if(p.x >= gridSize.x ||
       p.y >= gridSize.y ||
       p.z >= gridSize.z)
        return -1;

    return p.x +
           p.y * gridSize.x +
           p.z * gridSize.x * gridSize.y;
}

void main()
{
    ivec3 p = ivec3(gl_GlobalInvocationID);

    if(p.x >= gridSize.x ||
       p.y >= gridSize.y ||
       p.z >= gridSize.z)
        return;

    ivec3 dir = ivec3(0);

    if(direction == 0)
        dir = ivec3(1,0,0);
    else if(direction == 1)
        dir = ivec3(-1,0,0);
    else if(direction == 2)
        dir = ivec3(0,1,0);
    else if(direction == 3)
        dir = ivec3(0,-1,0);
    else if(direction == 4)
        dir = ivec3(0,0,1);
    else if(direction == 5)
        dir = ivec3(0,0,-1);

    ivec3 near = p + dir;

    int currentIdx = idx(p);
    int nearIdx = idx(near);

    mask[currentIdx] = 0;

    if(voxels[currentIdx].type != 0)
    {
        if(nearIdx == -1 ||
           voxels[nearIdx].type == 0)
        {
            mask[currentIdx] = 1;
        }
    }
}