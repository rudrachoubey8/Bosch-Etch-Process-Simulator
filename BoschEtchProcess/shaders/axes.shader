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

layout(std430, binding = 2) buffer Counter {
    uint vertCount;
};

uniform ivec3 gridSize;

int idx(int x,int y,int z) {
    return x + y*gridSize.x + z*gridSize.x*gridSize.y;
}
void main(){
    

}