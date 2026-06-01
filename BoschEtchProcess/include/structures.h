#pragma once
#include <vector>
#include <cstdint>
#include <imgui.h>
#include <string>

const int chunkSize = 32;

struct Voxel {
    float threshold;
    float depositThreshold;
    float voxelSize;
    int solid;
    int type;
};


struct Chunk {
    int chunkX = 0;
    int chunkY = 0;
    int chunkZ = 0;
    int dirty = 1;

    int vertexOffset = 0;
    int vertexCount = 0;
    int generatedCount = 0;
};


struct ParticleTypeData
{
    int count = 1000;
    float energy = 100.0f;
    float stddev = 5.0f;
    float halfAngle = 20.0f;

    bool deposit = false;
    bool draw = true;
};
struct Particle {
    int alive = 1;
    float x = 0,y = 0,z = 0;
    float dx = 0, dy = 0, dz = 0;

    int deposit = 0;
    float speed = 0;
    float energy = 20;
    int type = 0;
};

struct HitEvent {
    int cx, cy, cz;
    float damage;
    uint32_t flags; // 1 = deposit, 2 = absorbed
};

struct Vertex {
    float x, y, z, _pad0;
    float nx, ny, nz, _pad1;
    float r, g, b, _pad2;
};

// XYZ = width, length, depth
class Grid {
public:
    int X, Y, Z;
    std::vector<Voxel> voxels;

    Grid(int X, int Y, int Z);

    Voxel& at(int x, int y, int z);
    bool inBounds(int x, int y, int z);


private:

    int index(int x, int y, int z);
};
void RenderDynamicInputGrid(int& numCols, int& numRows, std::vector<float>& gridData, int offset);