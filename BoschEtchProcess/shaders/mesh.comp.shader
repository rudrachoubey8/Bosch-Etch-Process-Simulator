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

layout(std430, binding = 6) readonly buffer Voxels
{
    Voxel voxels[];
};

layout(rgba8, binding = 4) uniform writeonly image2D outputImage;

uniform ivec3 gridSize;
uniform vec3 bounds;
uniform vec3 center;
uniform ivec2 tileOffset;

uniform vec3 rayOrigin;
uniform mat3 viewMatrix;

int idx(int x, int y, int z)
{
    return x + y * gridSize.x + z * gridSize.x * gridSize.y;
}

bool inBounds(int x, int y, int z)
{
    return x >= 0 && y >= 0 && z >= 0 &&
           x < gridSize.x && y < gridSize.y && z < gridSize.z;
}

bool solidAt(int x, int y, int z)
{
    if (!inBounds(x, y, z)) return false;
    return voxels[idx(x, y, z)].solid != 0;
}

int typeAt(int x, int y, int z)
{
    if (!inBounds(x, y, z)) return 0;
    return voxels[idx(x, y, z)].type;
}

vec3 colorFromType(int t)
{
    if (t == 0) return vec3(1.0, 0.647, 0.0);
    if (t == 1) return vec3(0.0, 1.0, 0.0);
    if (t == 2) return vec3(0.5, 0.0, 0.5);
    if (t == 3) return vec3(0.0, 1.0, 1.0);
    return vec3(1.0);
}


// Wider trilinear normal — samples a 3x3x3 neighbourhood
// with Gaussian-weighted Sobel for much softer shading
vec3 voxelNormal(ivec3 p)
{
    float nx = 0.0, ny = 0.0, nz = 0.0;

    for(int dz = -1; dz <= 1; dz++)
    for(int dy = -1; dy <= 1; dy++)
    for(int dx = -1; dx <= 1; dx++)
    {
        float w =
            (abs(dx) == 0 ? 2.0 : 1.0) *
            (abs(dy) == 0 ? 2.0 : 1.0) *
            (abs(dz) == 0 ? 2.0 : 1.0);

        float s = float(solidAt(
            p.x + dx,
            p.y + dy,
            p.z + dz
        ));

        nx += dx * w * s;
        ny += dy * w * s;
        nz += dz * w * s;
    }

    vec3 n = vec3(nx, ny, nz);

    if(length(n) < 0.0001)
    {
        // boundary fallback from before
        vec3 distToMin = vec3(p) / vec3(gridSize);
        vec3 distToMax = 1.0 - distToMin;
        vec3 dist = min(distToMin, distToMax);

        int axis = 0;
        if(dist.y < dist.x && dist.y < dist.z) axis = 1;
        if(dist.z < dist.x && dist.z < dist.y) axis = 2;

        vec3 fallback = vec3(0.0);
        if(axis == 0) fallback.x = (distToMin.x < distToMax.x) ? -1.0 : 1.0;
        if(axis == 1) fallback.y = (distToMin.y < distToMax.y) ? -1.0 : 1.0;
        if(axis == 2) fallback.z = (distToMin.z < distToMax.z) ? -1.0 : 1.0;

        return fallback;
    }

    return normalize(-n);
}


vec2 rayAABB(vec3 ro, vec3 rd, vec3 boxMin, vec3 boxMax)
{
    vec3 invDir = 1.0 / rd;
    vec3 t0 = (boxMin - ro) * invDir;
    vec3 t1 = (boxMax - ro) * invDir;
    vec3 tMin = min(t0, t1);
    vec3 tMax = max(t0, t1);
    float tEnter = max(max(tMin.x, tMin.y), tMin.z);
    float tExit  = min(min(tMax.x, tMax.y), tMax.z);
    return vec2(tEnter, tExit);
}

void main()
{
    ivec2 pixel = tileOffset + ivec2(gl_GlobalInvocationID.xy);
    ivec2 screenSize = imageSize(outputImage);

    if (pixel.x >= screenSize.x || pixel.y >= screenSize.y)
        return;

    vec2 uv = (vec2(pixel) / vec2(screenSize)) * 2.0 - 1.0;
    uv.x *= float(screenSize.x) / float(screenSize.y);

    vec3 screenPos = vec3(uv.x, uv.y, 0.0);
    vec3 rayDir = normalize(viewMatrix * vec3(uv.x, uv.y, 1.0));

    float voxelSize = bounds.x / float(gridSize.x);
    float stepSize  = voxelSize * 0.5;

    vec4 outColor = vec4(0, 0, 0, 1);

    vec3 boxMin = center - bounds * 0.5;
    vec3 boxMax = center + bounds * 0.5;

    vec2 tHit = rayAABB(rayOrigin, rayDir, boxMin, boxMax);

    if (tHit.x > tHit.y || tHit.y < 0.0)
    {
        imageStore(outputImage, pixel, outColor);
        return;
    }

    float t = max(tHit.x, 0.0) + stepSize * 0.01;
    float tMax = tHit.y;

    for (int i = 0; i < 512; i++)
    {
        if (t > tMax) break;

        vec3 p = rayOrigin + rayDir * t;

        vec3 local = (p - boxMin) / bounds;
        ivec3 voxel = ivec3(local * vec3(gridSize));

        if (!inBounds(voxel.x, voxel.y, voxel.z))
            break;
        if(solidAt(voxel.x, voxel.y, voxel.z))
        {
            vec3 N = voxelNormal(voxel);
            vec3 L = normalize(rayOrigin - p);
            vec3 V = -rayDir;
            vec3 H = normalize(L + V);

            float diffuse = max(dot(N, L), 0.0);

            float specular = pow(max(dot(N, H), 0.0), 32.0) * 0.4;

            float occlusion = 0.0;
            for(int oz = -1; oz <= 1; oz++)
            for(int oy = -1; oy <= 1; oy++)
            for(int ox = -1; ox <= 1; ox++)
                occlusion += float(solidAt(
                    voxel.x + ox,
                    voxel.y + oy,
                    voxel.z + oz
                ));

            occlusion = 1.0 - (occlusion / 27.0) * 0.6;

            float ambient = 0.15;

            vec3 baseColor = colorFromType(
                typeAt(voxel.x, voxel.y, voxel.z)
            );

            vec3 color =
                baseColor * (ambient + diffuse) * occlusion
                + specular;

            float fog = exp(-t * 0.08);
            vec3 fogColor = vec3(0.05, 0.05, 0.08); 
            color = mix(fogColor, color, fog);

            outColor = vec4(color, 1.0);
            break;
        }
        

        t += stepSize;
    }

    imageStore(outputImage, pixel, outColor);
}