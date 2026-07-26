#version 430
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

struct Voxel
{
    float threshold;
    float depositThreshold;
    float voxelSize;
    int solid;
    int type;
};

layout(std430, binding = 6) readonly buffer Voxels { Voxel voxels[]; };
layout(rgba8, binding = 4) uniform writeonly image2D outputImage;

uniform ivec3 gridSize;
uniform vec3 bounds;
uniform vec3 center;
uniform ivec2 tileOffset;
uniform vec3 rayOrigin;
uniform mat3 viewMatrix;
uniform int materialColorCount;
uniform vec3 materialColors[16];

uniform ivec2 slice;
uniform int showSlice;

int idx(int x, int y, int z)
{
    return x + y * gridSize.x + z * gridSize.x * gridSize.y;
}

bool inBounds(int x, int y, int z)
{
    return x >= 0 && y >= 0 && z >= 0 &&
           x < gridSize.x && y < gridSize.y && z < gridSize.z;
}

int solidAt(int x, int y, int z)
{
    if (!inBounds(x, y, z)) return 0;
    return voxels[idx(x, y, z)].solid;
}

int typeAt(int x, int y, int z)
{
    if (!inBounds(x, y, z)) return 0;
    return voxels[idx(x, y, z)].type;
}

vec3 colorFromType(int t)
{
    if (t >= 0 && t < materialColorCount && t < 16)
        return materialColors[t];
    return vec3(1.0);
}

float sampleSDF(ivec3 p)
{
    if (!inBounds(p.x, p.y, p.z)) return 1.0;
    if (solidAt(p.x, p.y, p.z) == 1) return -1.0;

    float minDist = 1.0;
    for (int dz = -1; dz <= 1; dz++)
    for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++)
    {
        if (dx == 0 && dy == 0 && dz == 0) continue;
        if (solidAt(p.x+dx, p.y+dy, p.z+dz) == 1)
        {
            float d = length(vec3(dx, dy, dz));
            minDist = min(minDist, d);
        }
    }
    return minDist;
}

// Gradient normal from fake SDF
vec3 computeNormal(ivec3 p)
{
    float nx = sampleSDF(ivec3(p.x+1, p.y, p.z)) - sampleSDF(ivec3(p.x-1, p.y, p.z));
    float ny = sampleSDF(ivec3(p.x, p.y+1, p.z)) - sampleSDF(ivec3(p.x, p.y-1, p.z));
    float nz = sampleSDF(ivec3(p.x, p.y, p.z+1)) - sampleSDF(ivec3(p.x, p.y, p.z-1));
    return normalize(vec3(nx, ny, nz));
}

vec2 rayAABB(vec3 ro, vec3 rd, vec3 boxMin, vec3 boxMax)
{
    vec3 invDir = 1.0 / rd;
    vec3 t0 = (boxMin - ro) * invDir;
    vec3 t1 = (boxMax - ro) * invDir;
    vec3 tMin = min(t0, t1);
    vec3 tMax = max(t0, t1);

    return vec2(
        max(max(tMin.x, tMin.y), tMin.z),
        min(min(tMax.x, tMax.y), tMax.z)
    );
}
bool ddaTrace(vec3 ro, vec3 rd, vec3 boxMin, float voxelSize, float tStart, float tMax, out ivec3 hitVoxel, out float hitT, out ivec3 hitFace)
{
    vec3 entry = ro + rd * tStart;
    vec3 local = (entry - boxMin) / voxelSize;

    ivec3 voxel = ivec3(clamp(floor(local), vec3(0), vec3(gridSize) - vec3(1)));

    ivec3 step = ivec3(sign(rd));
    vec3 deltaDist = abs(vec3(voxelSize) / rd);

    vec3 voxelCorner = boxMin + vec3(voxel) * voxelSize;
    vec3 tNext;

    tNext.x = (step.x > 0 ? (voxelCorner.x + voxelSize - entry.x) : (entry.x - voxelCorner.x)) / abs(rd.x);
    tNext.y = (step.y > 0 ? (voxelCorner.y + voxelSize - entry.y) : (entry.y - voxelCorner.y)) / abs(rd.y);
    tNext.z = (step.z > 0 ? (voxelCorner.z + voxelSize - entry.z) : (entry.z - voxelCorner.z)) / abs(rd.z);

    float t = tStart;

    // FIX: Explicitly derive the starting boundary normal based on which AABB side we hit
    ivec3 face = ivec3(0, 0, -step.z); 
    vec3 distanceToEdge = abs(entry - boxMin) / (float(gridSize) * voxelSize);
    
    // If we are right on the edge of the global bounding box, force the correct boundary normal
    if (abs(entry.x - boxMin.x) < 0.001) face = ivec3(-1, 0, 0);
    else if (abs(entry.x - (boxMin.x + float(gridSize.x) * voxelSize)) < 0.001) face = ivec3(1, 0, 0);
    else if (abs(entry.y - boxMin.y) < 0.001) face = ivec3(0, -1, 0);
    else if (abs(entry.y - (boxMin.y + float(gridSize.y) * voxelSize)) < 0.001) face = ivec3(0, 1, 0);
    else if (abs(entry.z - boxMin.z) < 0.001) face = ivec3(0, 0, -1);
    else if (abs(entry.z - (boxMin.z + float(gridSize.z) * voxelSize)) < 0.001) face = ivec3(0, 0, 1);

    for (int i = 0; i < 1024; i++)
    {
        if (!inBounds(voxel.x, voxel.y, voxel.z)) break;
        if (t > tMax) break;

        int planeCoord = voxel.z;
        if(slice.x == 1) planeCoord = voxel.y;
        else if(slice.x == 2) planeCoord = voxel.x;

        bool visibleInMode =
            showSlice == 0 ||
            planeCoord <= slice.y;

        if (solidAt(voxel.x, voxel.y, voxel.z) == 1 && visibleInMode)
        {
            hitVoxel = voxel;
            hitT = t;
            hitFace = face;
            return true;
        }

        if (tNext.x <= tNext.y && tNext.x <= tNext.z)
        {
            t = tNext.x;
            face = ivec3(-step.x, 0, 0); // Correct inward-facing step direction
            voxel.x += step.x;
            tNext.x += deltaDist.x;
        }
        else if (tNext.y <= tNext.z)
        {
            t = tNext.y;
            face = ivec3(0, -step.y, 0);
            voxel.y += step.y;
            tNext.y += deltaDist.y;
        }
        else
        {
            t = tNext.z;
            face = ivec3(0, 0, -step.z);
            voxel.z += step.z;
            tNext.z += deltaDist.z;
        }
    }

    hitVoxel = ivec3(0);
    hitT = tMax;
    hitFace = ivec3(0,0,1);
    return false;
}
void main()
{
    ivec2 pixel = tileOffset + ivec2(gl_GlobalInvocationID.xy);
    ivec2 screenSize = imageSize(outputImage);
    if (pixel.x >= screenSize.x || pixel.y >= screenSize.y) return;

    vec2 uv = (vec2(pixel) / vec2(screenSize)) * 2.0 - 1.0;
    uv.x *= float(screenSize.x) / float(screenSize.y);

    vec3 rd = normalize(viewMatrix * vec3(uv.x, uv.y, 1.0));
    vec3 ro = rayOrigin;

    vec3 boxMin = center - bounds * 0.5;
    vec3 boxMax = center + bounds * 0.5;
    float voxelSize = bounds.x / float(gridSize.x);

    vec2 tHit = rayAABB(ro, rd, boxMin, boxMax);

    if (tHit.x > tHit.y || tHit.y < 0.0) return;

    float tStart = max(tHit.x, 0.0);
  
    ivec3 hitVoxel;
    float hitT;
    ivec3 hitFace;

    bool hit = ddaTrace(ro, rd, boxMin, voxelSize, tStart, tHit.y, hitVoxel, hitT, hitFace);

    if (!hit) return;

    vec3 hitPos    = ro + rd * hitT;
    vec3 hitNormal = normalize(vec3(hitFace));  
    vec3 baseColor = colorFromType(typeAt(hitVoxel.x, hitVoxel.y, hitVoxel.z));
    int hitPlaneCoord = hitVoxel.z;
    if(slice.x == 1) hitPlaneCoord = hitVoxel.y;
    else if(slice.x == 2) hitPlaneCoord = hitVoxel.x;

    if(showSlice != 0 && hitPlaneCoord == slice.y)
        baseColor = mix(baseColor, vec3(1.0, 0.05, 0.03), 0.55);

    vec3 L        = normalize(rayOrigin - hitPos);
    float diffuse = max(dot(hitNormal, L), 0.0);
    float ambient = 0.15;

    imageStore(outputImage, pixel, vec4(baseColor * (ambient + diffuse), 1.0));
}
