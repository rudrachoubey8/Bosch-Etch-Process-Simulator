#version 430

layout(local_size_x = 16, local_size_y = 16) in;

struct Voxel
{
    float threshold;
    float depositThreshold;
    float voxelSize;
    int solid;
    int type;
};

layout(std430, binding = 6) readonly buffer Voxels
{
    Voxel voxels[];
};

layout(std430, binding = 13) buffer Slice
{
    int slice[];
};

uniform ivec3 gridSize;

uniform int plane;      // 0=XY, 1=XZ, 2=YZ
uniform int sliceIndex;

uint voxelIndex(int x, int y, int z)
{
    return uint(x + y * gridSize.x + z * gridSize.x * gridSize.y);
}

void main()
{
    uint u = gl_GlobalInvocationID.x;
    uint v = gl_GlobalInvocationID.y;

    int x, y, z;
    uint dst;

    if (plane == 0) // XY
    {
        if (u >= uint(gridSize.x) || v >= uint(gridSize.y))
            return;

        x = int(u);
        y = int(v);
        z = sliceIndex;

        dst = u + v * uint(gridSize.x);
    }
    else if (plane == 1) // XZ
    {
        if (u >= uint(gridSize.x) || v >= uint(gridSize.z))
            return;

        x = int(u);
        y = sliceIndex;
        z = int(v);

        dst = u + v * uint(gridSize.x);
    }
    else // YZ
    {
        if (u >= uint(gridSize.y) || v >= uint(gridSize.z))
            return;

        x = sliceIndex;
        y = int(u);
        z = int(v);

        dst = u + v * uint(gridSize.y);
    }
    if(voxels[voxelIndex(x, y, z)].solid == 1){
        slice[dst] = voxels[voxelIndex(x, y, z)].type;
    }
    else {
        slice[dst] =  -1;
        }
}