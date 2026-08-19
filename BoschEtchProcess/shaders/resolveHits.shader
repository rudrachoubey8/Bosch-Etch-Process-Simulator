#version 430

layout(local_size_x = 256) in;

#define MAX_HITS 50000u
#define MAX_SPUTTER_LAYERS 16

#define MAX_SPUTTER_NEIGHBORS 8
#define MAX_SPUTTER_PATCH_VOXELS (MAX_SPUTTER_NEIGHBORS + 1)

struct Voxel
{
    float threshold;
    float depositThreshold;
    float voxelSize;
    int solid;
    int type;
};

struct HitEvent
{
    int cx;
    int cy;
    int cz;
    float damage;
    uint flags;
    int depositVoxelType;
    int nx;
    int ny;
    int nz;
    float incidenceCos;
};

layout(std430, binding = 6) buffer VoxelBuffer
{
    Voxel voxels[];
};

layout(std430, binding = 7) readonly buffer HitBuffer
{
    HitEvent hits[];
};

layout(std430, binding = 8) readonly buffer HitCounter
{
    uint hitCount;
};

uniform ivec3 gridSize;
uniform int voxelTypeCount;
uniform float voxelThresholds[16];
uniform float voxelDepositThresholds[16];
uniform float sputterYieldScale;
uniform float sputterEnergyDamping;
uniform float minSputterEnergy;

int voxelIndex(ivec3 c)
{
    return c.x + c.y * gridSize.x + c.z * gridSize.x * gridSize.y;
}

bool inBounds(ivec3 c)
{
    return all(greaterThanEqual(c, ivec3(0))) &&
        all(lessThan(c, gridSize));
}

uint hash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float random01(uint seed)
{
    return float(hash(seed)) / 4294967295.0;
}

ivec3 faceNeighbor(int direction)
{
    if(direction == 0) return ivec3( 1, 0, 0);
    if(direction == 1) return ivec3(-1, 0, 0);
    if(direction == 2) return ivec3( 0, 1, 0);
    if(direction == 3) return ivec3( 0,-1, 0);
    if(direction == 4) return ivec3( 0, 0, 1);
    return ivec3(0, 0,-1);
}

bool isSurfaceVoxel(ivec3 c)
{
    if(!inBounds(c))
        return false;

    Voxel v = voxels[voxelIndex(c)];
    if(v.solid == 0)
        return false;

    for(int direction = 0; direction < 6; ++direction)
    {
        ivec3 n = c + faceNeighbor(direction);
        if(!inBounds(n))
            return true;
        if(voxels[voxelIndex(n)].solid == 0)
            return true;
    }

    return false;
}

float yamamuraAngularFactor(float incidenceCos)
{
    float c = clamp(incidenceCos, 0.05, 1.0);
    return pow(c, -1.0) * exp(-2.0 * (1.0 / c - 1.0));
}

float sputterYield(float energy, float eth, float incidenceCos)
{
    if(energy <= eth || energy < minSputterEnergy)
        return 0.0;

    return sputterYieldScale *
        max(sqrt(energy) - sqrt(max(eth, 0.0)), 0.0) *
        yamamuraAngularFactor(incidenceCos);
}

ivec3 tangentOffset(ivec3 normal, int index)
{
    int a = index / 3 - 1;
    int b = index % 3 - 1;

    if(abs(normal.x) > 0)
        return ivec3(0, a, b);
    if(abs(normal.y) > 0)
        return ivec3(a, 0, b);
    return ivec3(a, b, 0);
}

int orderedPatchIndex(int order, uint seed)
{
    if(order == 0)
        return 4;

    int ringIndex = (order - 1 + int(seed % 8u)) % 8;
    return ringIndex < 4 ? ringIndex : ringIndex + 1;
}

bool removeSurfaceVoxel(ivec3 c)
{
    if(!inBounds(c))
        return false;

    int idx = voxelIndex(c);
    Voxel v = voxels[idx];
    if(v.solid == 0)
        return false;

    if(!isSurfaceVoxel(c))
        return false;

    int previousSolid = atomicExchange(voxels[idx].solid, 0);
    if(previousSolid == 0)
        return false;

    voxels[idx].type = 0;
    return true;
}

int removeUnsupportedNear(ivec3 center, ivec3 normal, int maxRemovals)
{
    ivec3 down = ivec3(0, -1, 0);
    int removed = 0;

    int patchVoxels = min(MAX_SPUTTER_PATCH_VOXELS, 9);

    for(int order = 0; order < patchVoxels && removed < maxRemovals; ++order)
    {
        ivec3 c = center + tangentOffset(normal, order);
        if(!inBounds(c))
            continue;

        int idx = voxelIndex(c);
        Voxel v = voxels[idx];
        if(v.solid == 0)
            continue;

        ivec3 support = c + down;
        if(inBounds(support) && voxels[voxelIndex(support)].solid != 0)
            continue;

        int lateralSupport = 0;
        for(int direction = 0; direction < 6; ++direction)
        {
            ivec3 n = c + faceNeighbor(direction);
            if(!inBounds(n) || all(equal(n, support)))
                continue;

            Voxel nv = voxels[voxelIndex(n)];
            if(nv.solid != 0)
                ++lateralSupport;
        }

        if(lateralSupport <= 1)
        {
            if(removeSurfaceVoxel(c))
                ++removed;
        }
    }

    return removed;
}

bool trySputterCandidate(
    ivec3 candidate,
    float energy,
    float incidenceCos)
{
    if(!inBounds(candidate))
        return false;

    Voxel candidateVoxel = voxels[voxelIndex(candidate)];
    if(candidateVoxel.solid == 0)
        return false;

    float y = sputterYield(
        energy,
        candidateVoxel.threshold,
        incidenceCos);
    if(y <= 0.0)
        return false;

    return removeSurfaceVoxel(candidate);
}

void sputterSurfacePatch(
    ivec3 start,
    ivec3 normal,
    float initialEnergy,
    float incidenceCos,
    uint seed)
{
    if(!inBounds(start))
        return;

    Voxel startVoxel = voxels[voxelIndex(start)];
    if(startVoxel.solid == 0)
        return;

    float damping = clamp(sputterEnergyDamping, 0.0, 1.0);

    for(int layer = 0; layer < MAX_SPUTTER_LAYERS; ++layer)
    {
        float layerEnergy = initialEnergy * pow(damping, float(layer));
        if(layerEnergy < minSputterEnergy)
            break;

        ivec3 layerCenter = start - normal * layer;
        if(!inBounds(layerCenter))
            break;

        Voxel layerVoxel = voxels[voxelIndex(layerCenter)];
        if(layerVoxel.solid == 0)
            break;

        float y = sputterYield(
            layerEnergy,
            layerVoxel.threshold,
            incidenceCos);
        if(y <= 0.0)
            break;

        int guaranteed = int(floor(y));
        float fractional = fract(y);
        int removed = 0;
        int patchVoxels = min(MAX_SPUTTER_PATCH_VOXELS, 9);
        int targetRemovals = min(patchVoxels, guaranteed);
        if(fractional > 0.0 &&
           random01(seed ^ uint(layer * 92837111) ^ uint(voxelIndex(layerCenter))) < fractional)
        {
            targetRemovals = min(patchVoxels, targetRemovals + 1);
        }

        for(int order = 0; order < patchVoxels && removed < targetRemovals; ++order)
        {
            int patchIndex = orderedPatchIndex(order, seed + uint(layer * 17));
            ivec3 candidate = layerCenter + tangentOffset(normal, patchIndex);
            if(trySputterCandidate(
                candidate,
                layerEnergy,
                incidenceCos))
                ++removed;
        }

        removed += removeUnsupportedNear(
            layerCenter,
            normal,
            patchVoxels - removed);

        if(removed == 0)
            break;
    }
}

void main()
{
    uint id = gl_GlobalInvocationID.x;
    uint actualHitCount = min(hitCount, MAX_HITS);
    if(id >= actualHitCount)
        return;

    HitEvent h = hits[id];
    ivec3 cell = ivec3(h.cx, h.cy, h.cz);
    if(!inBounds(cell))
        return;

    int vidx = voxelIndex(cell);
    Voxel v = voxels[vidx];
    if(v.solid == 0)
        return;

    if((h.flags & 1u) != 0u)
    {
        v.depositThreshold -= h.damage;

        if(v.depositThreshold <= 0.0)
        {
            int materialType = clamp(h.depositVoxelType, 0, max(voxelTypeCount - 1, 0));
            v.solid = 1;
            v.type = materialType;
            v.threshold = voxelThresholds[materialType];
            v.depositThreshold = voxelDepositThresholds[materialType];
        }

        voxels[vidx] = v;
        return;
    }

    ivec3 normal = ivec3(h.nx, h.ny, h.nz);
    if(all(equal(normal, ivec3(0))))
        normal = ivec3(0, 1, 0);

    uint seed = hash(
        id ^
        uint(h.cx * 73856093) ^
        uint(h.cy * 19349663) ^
        uint(h.cz * 83492791));

    sputterSurfacePatch(
        cell,
        normal,
        max(h.damage, 0.0),
        clamp(h.incidenceCos, 0.05, 1.0),
        seed);
}
