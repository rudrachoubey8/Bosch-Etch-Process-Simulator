#pragma once
#include <vector>
#include <cstdint>

struct Voxel {
    int32_t solid;
    int32_t type;
    float threshold;
    float depositThreshold;
    float voxelSize;
};

struct Particle {
    bool alive = 1;
    float x = 0,y = 0,z = 0;
    float dx = 0, dy = 0, dz = 0;

    bool deposit = 0;
    float speed = 0;
    float energy = 20;
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
