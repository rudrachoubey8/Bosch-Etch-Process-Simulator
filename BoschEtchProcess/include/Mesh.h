#pragma once
#include <vector>
#include "structures.h"

using namespace std;


class Mesh {
public:
    Mesh(Grid& g);

    void buildMesh();
    std::vector<Vertex> vertices;

private:
    Grid& grid;

    enum Axis { X_AXIS, Y_AXIS, Z_AXIS };

    struct Dir {
        Axis axis;
        int sign; // +1 or -1
    };

    void buildMask(const Dir& d, int slice,
        std::vector<std::vector<int>>& mask);

    void greedyMerge(const Dir& d, int slice,
        std::vector<std::vector<int>>& mask);

    void emitQuad(const Dir& d, int slice,
        int a, int b, int w, int h, int type);

    bool solid(int x, int y, int z);
};