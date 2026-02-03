#pragma once
#include <vector>

struct Voxel {
    bool solid = 0;
    int type = 0;
    int threshold = 0;
    int depositThreshold = 0;
    int voxelSize = 0;
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
    float x, y, z;
    float nx, ny, nz;
    float r, g, b;
};

// XYZ = width, length, depth
class Grid {
public:
    int X, Y, Z;

    Grid(int X, int Y, int Z);

    Voxel& at(int x, int y, int z);
    bool inBounds(int x, int y, int z);


private:
    std::vector<Voxel> voxels;

    int index(int x, int y, int z);
};
