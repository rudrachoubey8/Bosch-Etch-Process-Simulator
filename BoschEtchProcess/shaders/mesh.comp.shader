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

int typeAt(int x, int y, int z)
{
    if (!inBounds(x, y, z)) return -1;
    return voxels[idx(x, y, z)].type;
}

float sdfAtSafe(int x, int y, int z)
{
    if (!inBounds(x, y, z)) return 1.0;
    float s = voxels[idx(x, y, z)].sdf;
    if (s > 1e6) return 1.0;
    return s;
}

vec3 colorFromType(int t)
{
    if (t == 0) return vec3(1.0, 0.647, 0.0);
    if (t == 1) return vec3(0.0, 1.0, 0.0);
    if (t == 2) return vec3(0.5, 0.0, 0.5);
    if (t == 3) return vec3(0.0, 1.0, 1.0);
    return vec3(1.0);
}

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

vec3 computeNormal(vec3 worldPos, vec3 boxMin, float voxelSize)
{
    float eps = voxelSize * 0.1;
    float dx = sampleSDFTrilinear(worldPos + vec3( eps,0,0), boxMin, voxelSize)
             - sampleSDFTrilinear(worldPos + vec3(-eps,0,0), boxMin, voxelSize);
    float dy = sampleSDFTrilinear(worldPos + vec3(0, eps,0), boxMin, voxelSize)
             - sampleSDFTrilinear(worldPos + vec3(0,-eps,0), boxMin, voxelSize);
    float dz = sampleSDFTrilinear(worldPos + vec3(0,0, eps), boxMin, voxelSize)
             - sampleSDFTrilinear(worldPos + vec3(0,0,-eps), boxMin, voxelSize);
    vec3 n = vec3(dx, dy, dz);
    if (length(n) < 0.001) return vec3(0, 1, 0);
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

bool trace(vec3 ro, vec3 rd, vec3 boxMin, float voxelSize, float tStart, float tMax,
           out ivec3 hitVoxel, out float hitT)
{
    vec3 entry = ro + rd * tStart;
    vec3 local = (entry - boxMin) / voxelSize;
    ivec3 voxel = ivec3(clamp(floor(local), vec3(0), vec3(gridSize) - vec3(1)));

    ivec3 step     = ivec3(sign(rd));
    vec3 deltaDist = abs(vec3(voxelSize) / rd);

    vec3 voxelCorner = boxMin + vec3(voxel) * voxelSize;
    vec3 tNext;
    tNext.x = tStart + ((step.x > 0) ? (voxelCorner.x + voxelSize - entry.x) : (entry.x - voxelCorner.x)) / abs(rd.x);
    tNext.y = tStart + ((step.y > 0) ? (voxelCorner.y + voxelSize - entry.y) : (entry.y - voxelCorner.y)) / abs(rd.y);
    tNext.z = tStart + ((step.z > 0) ? (voxelCorner.z + voxelSize - entry.z) : (entry.z - voxelCorner.z)) / abs(rd.z);

    float t = tStart;

    ivec3 range = ivec3(gridSize); // default: no slice
    if      (slice.x == 0) range = ivec3(gridSize.x, gridSize.y, slice.y);
    else if (slice.x == 1) range = ivec3(gridSize.x, slice.y,    gridSize.z);
    else if (slice.x == 2) range = ivec3(slice.y,    gridSize.y, gridSize.z);

    for (int i = 0; i < 1024; i++)
    {
        if (!inBounds(voxel.x, voxel.y, voxel.z)) break;
        if (t > tMax) break;
        vec3 pos = ro + rd * t;
        local = (pos - boxMin) / voxelSize;
        voxel = ivec3(clamp(floor(local), vec3(0), vec3(gridSize) - vec3(1)));

        if (!any(greaterThan(voxel, range)))
        {
            float rawSDF = sampleSDFTrilinear(ro + rd * t, boxMin, voxelSize);

            // not uninitialized and SDF says surface here
            if (rawSDF <= 1e6 && rawSDF <= 0.0)
            {
                hitVoxel = voxel;
                hitT     = t;
                return true;
            }
        }
        t += 0.3 * voxelSize;
    }

    hitVoxel = ivec3(0);
    hitT     = tMax;
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

    bool hit = trace(ro, rd, boxMin, voxelSize, tStart, tHit.y, hitVoxel, hitT);
    if (!hit) return;

    vec3 hitPos    = ro + rd * hitT;
    vec3 baseColor = colorFromType(typeAt(hitVoxel.x, hitVoxel.y, hitVoxel.z));

    vec3 hitNormal = computeNormal(hitPos, boxMin, voxelSize);
    if (dot(hitNormal, -rd) < 0.0) hitNormal = -hitNormal;

    vec3  L       = normalize(rayOrigin - hitPos);
    float diffuse = max(dot(hitNormal, L), 0.0);
    float ambient = 0.2;

    imageStore(outputImage, pixel, vec4(baseColor * (ambient + diffuse), 1.0));
}