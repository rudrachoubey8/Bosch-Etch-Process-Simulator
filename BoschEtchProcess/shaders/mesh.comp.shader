#version 430

layout(local_size_x=8, local_size_y=8, local_size_z=8) in;

struct Voxel {
    int solid;
    int type;
    float threshold;
    float depositThreshold;
    float voxelSize;
};

struct Vertex {
    vec3 pos;
    vec3 normal;
    vec3 color;
};

layout(std430, binding = 0) readonly buffer Voxels {
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

bool solidAt(int x,int y,int z) {
    if (x<0||y<0||z<0||
        x>=gridSize.x||y>=gridSize.y||z>=gridSize.z)
        return false;
    return voxels[idx(x,y,z)].solid != 0;
}

vec3 colorFromType(int t) {
    if (t==1) return vec3(1,0,0);
    if (t==2) return vec3(1,1,1);
    return vec3(0);
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

void main() {

    ivec3 p = ivec3(gl_GlobalInvocationID);
    if (p.x>=gridSize.x||p.y>=gridSize.y||p.z>=gridSize.z) return;

    int id = idx(p.x,p.y,p.z);
    if (voxels[id].solid == 0) return;

    vec3 c = colorFromType(voxels[id].type);
    vec3 v = vec3(p);

    // +X
    if (!solidAt(p.x+1,p.y,p.z))
        emitQuad(v+vec3(1,0,0), vec3(0,1,0), vec3(0,0,1), vec3(1,0,0), c);

    // -X
    if (!solidAt(p.x-1,p.y,p.z))
        emitQuad(v, vec3(0,0,1), vec3(0,1,0), vec3(-1,0,0), c);

    // +Y
    if (!solidAt(p.x,p.y+1,p.z))
        emitQuad(v+vec3(0,1,0), vec3(1,0,0), vec3(0,0,1), vec3(0,1,0), c);

    // -Y
    if (!solidAt(p.x,p.y-1,p.z))
        emitQuad(v, vec3(1,0,0), vec3(0,0,1), vec3(0,-1,0), c);

    // +Z
    if (!solidAt(p.x,p.y,p.z+1))
        emitQuad(v+vec3(0,0,1), vec3(1,0,0), vec3(0,1,0), vec3(0,0,1), c);

    // -Z
    if (!solidAt(p.x,p.y,p.z-1))
        emitQuad(v, vec3(0,1,0), vec3(1,0,0), vec3(0,0,-1), c);

    

}