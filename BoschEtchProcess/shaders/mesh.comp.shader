#version 430
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

struct Voxel
{
    float threshold;
    float depositThreshold;
    float voxelSize;
    int solid;
    int type;
    float sdf;
};

layout(std430, binding = 6) readonly buffer Voxels { Voxel voxels[]; };
layout(rgba8, binding = 4) uniform writeonly image2D outputImage;

uniform ivec3 gridSize;
uniform vec3 bounds;
uniform vec3 center;
uniform ivec2 tileOffset;
uniform vec3 rayOrigin;
uniform mat3 viewMatrix;
uniform ivec2 slice;

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

// safe SDF read — air voxels and out of bounds return large value
float sdfAtSafe(int x, int y, int z)
{
    if (!inBounds(x, y, z)) return 0;

    if(voxels[idx(x,y,z)].sdf > 1000000 && voxels[idx(x,y,z)].solid == 0) {
        return -1;
        }

    if(voxels[idx(x,y,z)].solid == 1){
        return 0;
        }

    return voxels[idx(x, y, z)].sdf;
}

vec3 colorFromType(int t)
{
    if (t == 0) return vec3(1.0, 0.647, 0.0);
    if (t == 1) return vec3(0.0, 1.0, 0.0);
    if (t == 2) return vec3(0.5, 0.0, 0.5);
    if (t == 3) return vec3(0.0, 1.0, 1.0);
    return vec3(1.0);
}

// trilinear SDF sample — only bleeds across same-solid voxels
float sampleSDFTrilinear(vec3 worldPos, vec3 boxMin, float voxelSize)
{
    vec3 local = (worldPos - boxMin) / voxelSize - 0.5;
    ivec3 base = ivec3(floor(local));
    vec3 f     = fract(local);

    float c000 = sdfAtSafe(base.x,   base.y,   base.z  );
    float c100 = sdfAtSafe(base.x+1, base.y,   base.z  );
    float c010 = sdfAtSafe(base.x,   base.y+1, base.z  );
    float c110 = sdfAtSafe(base.x+1, base.y+1, base.z  );
    float c001 = sdfAtSafe(base.x,   base.y,   base.z+1);
    float c101 = sdfAtSafe(base.x+1, base.y,   base.z+1);
    float c011 = sdfAtSafe(base.x,   base.y+1, base.z+1);
    float c111 = sdfAtSafe(base.x+1, base.y+1, base.z+1);

    float x00 = mix(c000, c100, f.x);
    float x10 = mix(c010, c110, f.x);
    float x01 = mix(c001, c101, f.x);
    float x11 = mix(c011, c111, f.x);
    float y0  = mix(x00,  x10,  f.y);
    float y1  = mix(x01,  x11,  f.y);
    return mix(y0, y1, f.z);
}

vec3 computeNormal(ivec3 voxel, float voxelSize, ivec3 faceNormal)
{
    float dx = sdfAtSafe(voxel.x + 1, voxel.y, voxel.z)
             - sdfAtSafe(voxel.x - 1, voxel.y, voxel.z);

    float dy = sdfAtSafe(voxel.x, voxel.y + 1, voxel.z)
             - sdfAtSafe(voxel.x, voxel.y - 1, voxel.z);

    float dz = sdfAtSafe(voxel.x, voxel.y, voxel.z + 1)
             - sdfAtSafe(voxel.x, voxel.y, voxel.z - 1);

    vec3 n = vec3(dx, dy, dz);

    if (length(n) < 0.001)
        return normalize(vec3(faceNormal));

    return normalize(n);
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

bool ddaTrace(vec3 ro, vec3 rd, vec3 boxMin, float voxelSize, float tStart, float tMax,
              out ivec3 hitVoxel, out float hitT, out ivec3 hitFace)
{
    vec3 entry  = ro + rd * tStart;
    vec3 local  = (entry - boxMin) / voxelSize;
    ivec3 voxel = ivec3(clamp(floor(local), vec3(0), vec3(gridSize) - vec3(1)));

    ivec3 step     = ivec3(sign(rd));
    vec3 deltaDist = abs(vec3(voxelSize) / rd);

    vec3 voxelCorner = boxMin + vec3(voxel) * voxelSize;
    vec3 tNext;
    tNext.x = (step.x > 0 ? (voxelCorner.x + voxelSize - entry.x) : (entry.x - voxelCorner.x)) / abs(rd.x);
    tNext.y = (step.y > 0 ? (voxelCorner.y + voxelSize - entry.y) : (entry.y - voxelCorner.y)) / abs(rd.y);
    tNext.z = (step.z > 0 ? (voxelCorner.z + voxelSize - entry.z) : (entry.z - voxelCorner.z)) / abs(rd.z);

    float t    = tStart;
    ivec3 face = ivec3(0, 0, -step.z);

    if      (abs(entry.x - boxMin.x)                                   < 0.001) face = ivec3(-1,  0,  0);
    else if (abs(entry.x - (boxMin.x + float(gridSize.x) * voxelSize)) < 0.001) face = ivec3( 1,  0,  0);
    else if (abs(entry.y - boxMin.y)                                   < 0.001) face = ivec3( 0, -1,  0);
    else if (abs(entry.y - (boxMin.y + float(gridSize.y) * voxelSize)) < 0.001) face = ivec3( 0,  1,  0);
    else if (abs(entry.z - boxMin.z)                                   < 0.001) face = ivec3( 0,  0, -1);
    else if (abs(entry.z - (boxMin.z + float(gridSize.z) * voxelSize)) < 0.001) face = ivec3( 0,  0,  1);

    ivec3 range = ivec3(0);
    if      (slice.x == 0) range = ivec3(gridSize.x, gridSize.y, slice.y);
    else if (slice.x == 1) range = ivec3(gridSize.x, slice.y,    gridSize.z);
    else if (slice.x == 2) range = ivec3(slice.y,    gridSize.y, gridSize.z);

    for (int i = 0; i < 1024; i++)
    {
        if (!inBounds(voxel.x, voxel.y, voxel.z)) break;
        if (t > tMax) break;

        // hit when SDF <= 0 (on or inside surface)
        float sdfVal = inBounds(voxel.x, voxel.y, voxel.z)
                     ? voxels[idx(voxel.x, voxel.y, voxel.z)].sdf
                     : 1e9;

        if (sdfVal <= 0 &&
            !any(greaterThan(voxel, range)))
        {
            hitVoxel = voxel;
            hitT     = t;
            hitFace  = face;
            return true;
        }

        if (tNext.x <= tNext.y && tNext.x <= tNext.z)
        {
            t       = tNext.x;
            face    = ivec3(-step.x, 0, 0);
            voxel.x += step.x;
            tNext.x += deltaDist.x;
        }
        else if (tNext.y <= tNext.z)
        {
            t       = tNext.y;
            face    = ivec3(0, -step.y, 0);
            voxel.y += step.y;
            tNext.y += deltaDist.y;
        }
        else
        {
            t       = tNext.z;
            face    = ivec3(0, 0, -step.z);
            voxel.z += step.z;
            tNext.z += deltaDist.z;
        }
    }

    hitVoxel = ivec3(0);
    hitT     = tMax;
    hitFace  = ivec3(0, 0, 1);
    return false;
}

void main()
{
    ivec2 pixel      = tileOffset + ivec2(gl_GlobalInvocationID.xy);
    ivec2 screenSize = imageSize(outputImage);
    if (pixel.x >= screenSize.x || pixel.y >= screenSize.y) return;

    vec2 uv = (vec2(pixel) / vec2(screenSize)) * 2.0 - 1.0;
    uv.x *= float(screenSize.x) / float(screenSize.y);

    vec3 rd = normalize(viewMatrix * vec3(uv.x, uv.y, 1.0));
    vec3 ro = rayOrigin;

    vec3  boxMin    = center - bounds * 0.5;
    vec3  boxMax    = center + bounds * 0.5;
    float voxelSize = bounds.x / float(gridSize.x);

    vec2 tHit = rayAABB(ro, rd, boxMin, boxMax);
    if (tHit.x > tHit.y || tHit.y < 0.0) return;

    float tStart = max(tHit.x, 0.0);

    ivec3 hitVoxel;
    float hitT;
    ivec3 hitFace;

    bool hit = ddaTrace(ro, rd, boxMin, voxelSize, tStart, tHit.y,
                        hitVoxel, hitT, hitFace);
    if (!hit) return;

    vec3 hitPos    = ro + rd * hitT;
    vec3 baseColor = colorFromType(typeAt(hitVoxel.x, hitVoxel.y, hitVoxel.z));

    // smooth normal from trilinear SDF gradient, fallback to face normal
    vec3 hitNormal = computeNormal(hitVoxel, voxelSize, hitFace);

    // ensure normal faces camera
    if (dot(hitNormal, -rd) < 0.0) hitNormal = -hitNormal;

    vec3  L       = normalize(rayOrigin - hitPos);
    float diffuse = max(dot(hitNormal, L), 0.0);
    float ambient = 0.15;

    imageStore(outputImage, pixel, vec4(baseColor * (ambient + diffuse), 1.0));
}