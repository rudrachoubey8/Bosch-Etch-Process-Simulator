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
uniform int size;

int idx(int x,int y,int z) {
    return x + y*gridSize.x + z*gridSize.x*gridSize.y;
}
void emitQuad(vec3 base, vec3 du, vec3 dv, vec3 normal, vec3 color) {
    uint i = atomicAdd(vertCount, 6);

    verts[i+0] = Vertex(base,            normal, color);
    verts[i+1] = Vertex(base + du,       normal, color);
    verts[i+2] = Vertex(base + du + dv,  normal, color);

    verts[i+3] = Vertex(base,            normal, color);
    verts[i+4] = Vertex(base + du + dv,  normal, color);
    verts[i+5] = Vertex(base + dv,       normal, color);
}

void main(){
    
    ivec3 p = ivec3(gl_GlobalInvocationID);

    if(p.x >= gridSize.x || p.y >= gridSize.y || p.z >= gridSize.z) return;

    emitQuad(
        vec3(p.x * size - gridSize.x * size / 2 + gridSize.x/2, -1, p.z * size - gridSize.z * size/2 + gridSize.z / 2), 
        size * vec3(1,0,0), 
        size * vec3(0,0,1), 
        vec3(0,1,0), 
        vec3(1,1,1)
    );

}