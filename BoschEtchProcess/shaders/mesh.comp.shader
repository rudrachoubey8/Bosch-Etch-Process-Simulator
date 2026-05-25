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
uniform int direction;

int idx(int x,int y,int z)
{
    return x +
           y * gridSize.x +
           z * gridSize.x * gridSize.y;
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

    return voxels[idx(x,y,z)].solid != 0;
}

int typeAt(int x,int y,int z)
{
    if(!inBounds(x,y,z))
        return -1;

    return voxels[idx(x,y,z)].type;
}

vec3 colorFromType(int t)
{
    if (t==0) return vec3(1,0.647,0);
    if (t==1) return vec3(0,1,0);
    if (t==2) return vec3(0.5,0,0.5);
    if (t==3) return vec3(0,1,1);

    return vec3(1);
}

void emitQuad(
    vec3 base,
    vec3 du,
    vec3 dv,
    vec3 normal,
    vec3 color
){
    uint i = atomicAdd(vertCount, 6);

    verts[i+0] = Vertex(base,           normal, color);
    verts[i+1] = Vertex(base + du,      normal, color);
    verts[i+2] = Vertex(base + du + dv, normal, color);

    verts[i+3] = Vertex(base,           normal, color);
    verts[i+4] = Vertex(base + du + dv, normal, color);
    verts[i+5] = Vertex(base + dv,      normal, color);
}

ivec3 faceNormal()
{
    if(direction == 0) return ivec3( 1, 0, 0);
    if(direction == 1) return ivec3(-1, 0, 0);
    if(direction == 2) return ivec3( 0, 1, 0);
    if(direction == 3) return ivec3( 0,-1, 0);
    if(direction == 4) return ivec3( 0, 0, 1);

    return ivec3(0,0,-1);
}

void getAxes(out ivec3 U, out ivec3 V)
{
    // X faces -> merge in YZ
    if(direction <= 1)
    {
        U = ivec3(0,1,0);
        V = ivec3(0,0,1);
    }
    // Y faces -> merge in XZ
    else if(direction <= 3)
    {
        U = ivec3(1,0,0);
        V = ivec3(0,0,1);
    }
    // Z faces -> merge in XY
    else
    {
        U = ivec3(1,0,0);
        V = ivec3(0,1,0);
    }
}

bool visibleFace(int x,int y,int z)
{
    ivec3 n = faceNormal();

    return
        solidAt(x,y,z) &&
        !solidAt(
            x + n.x,
            y + n.y,
            z + n.z
        );
}

bool mergeable(
    int x1,int y1,int z1,
    int x2,int y2,int z2
){
    return
        visibleFace(x1,y1,z1) &&
        visibleFace(x2,y2,z2) &&
        typeAt(x1,y1,z1) ==
        typeAt(x2,y2,z2);
}

void main()
{
    ivec3 p = ivec3(gl_GlobalInvocationID);

    if(!inBounds(p.x,p.y,p.z))
        return;

    if(!visibleFace(p.x,p.y,p.z))
        return;

    int myType = typeAt(p.x,p.y,p.z);

    ivec3 U, V;
    getAxes(U,V);

    //
    // OWNER TEST
    //

    if(mergeable(
        p.x,p.y,p.z,
        p.x - U.x,
        p.y - U.y,
        p.z - U.z
    ))
        return;

    if(mergeable(
        p.x,p.y,p.z,
        p.x - V.x,
        p.y - V.y,
        p.z - V.z
    ))
        return;

    //
    // WIDTH
    //

    int width = 1;

    while(true)
    {
        ivec3 q = p + V * width;

        if(!mergeable(
            p.x,p.y,p.z,
            q.x,q.y,q.z
        ))
            break;

        width++;
    }

    //
    // HEIGHT
    //

    int height = 1;

    while(true)
    {
        bool valid = true;

        for(int w = 0; w < width; w++)
        {
            ivec3 q =
                p +
                U * height +
                V * w;

            if(!mergeable(
                p.x,p.y,p.z,
                q.x,q.y,q.z
            ))
            {
                valid = false;
                break;
            }
        }

        if(!valid)
            break;

        height++;
    }

    vec3 normal = vec3(faceNormal());

    vec3 base = vec3(p);

    if(direction == 0)
        base += vec3(1,0,0);

    if(direction == 2)
        base += vec3(0,1,0);

    if(direction == 4)
        base += vec3(0,0,1);

    vec3 du = vec3(U) * float(height);
    vec3 dv = vec3(V) * float(width);

    vec3 color = colorFromType(myType);

    emitQuad(
        base,
        du,
        dv,
        normal,
        color
    );
}