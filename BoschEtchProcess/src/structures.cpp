#include "structures.h"

Grid::Grid(int X_, int Y_, int Z_)
    : X(X_), Y(Y_), Z(Z_), voxels(X_* Y_* Z_) {}

int Grid::index(int x, int y, int z) {
    return x + X * (y + Y * z);
}

Voxel& Grid::at(int x, int y, int z) {
    return voxels[index(x, y, z)];
}

bool Grid::inBounds(int x, int y, int z) {
    return (x < X && y < Y && z < Z && x >= 0 && y >= 0 && z >= 0);
}