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

bool solidAt(int x,int y,int z) {
    if (x<0||y<0||z<0||
        x>=gridSize.x||y>=gridSize.y||z>=gridSize.z)
        return false;
    return voxels[idx(x,y,z)].solid != 0;
}

vec3 colorFromType(int id, int t) {
    if (t==1) return vec3(id/100.0,(1 - id/100.0),0.5);
    if (t==2) return vec3(1,1,1);
    if (t==3) return vec3(1,1,0); 
    if (t==4) return vec3(1,0,1); 
    return vec3(1);
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

    
    if (voxels[id].solid == 0 ) return;

    bool surrounded = voxels[idx(p.x+1, p.y, p.z)].solid == 1 && voxels[idx(p.x, p.y+1, p.z)].solid == 1 && 
    voxels[idx(p.x, p.y, p.z+1)].solid == 1 && voxels[idx(p.x-1, p.y, p.z)].solid == 1 && voxels[idx(p.x, p.y-1, p.z)].solid == 1&& voxels[idx(p.x, p.y, p.z-1)].solid == 1;

    if(p.x - 1 < 0 || p.x + 1 >= gridSize.x || p.z - 1 < 0 || p.z + 1 >= gridSize.y || p.z - 1 < 0 || p.z + 1 >= gridSize.z)
    {
        surrounded = false;
        }
    if(surrounded) return;

    vec3 c = colorFromType(p.y, voxels[id].type);
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