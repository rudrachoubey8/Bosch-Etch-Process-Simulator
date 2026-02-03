#include <Mesh.h>
Mesh::Mesh(Grid& g) : grid(g) {}

bool Mesh::solid(int x, int y, int z) {
    if (!grid.inBounds(x, y, z)) return false;
    return grid.at(x, y, z).solid;
}

void Mesh::buildMask(
    const Dir& d,
    int slice,
    std::vector<std::vector<int>>& mask
) {
    int A = mask.size();
    int B = mask[0].size();

    for (int a = 0; a < A; a++) {
        for (int b = 0; b < B; b++) {

            int x, y, z;
            int nx, ny, nz;

            if (d.axis == X_AXIS) {
                x = slice; y = a; z = b;
                nx = slice + d.sign; ny = a; nz = b;
            }
            else if (d.axis == Y_AXIS) {
                x = a; y = slice; z = b;
                nx = a; ny = slice + d.sign; nz = b;
            }
            else {
                x = a; y = b; z = slice;
                nx = a; ny = b; nz = slice + d.sign;
            }

            bool curr = solid(x, y, z);
            bool next = solid(nx, ny, nz);

            mask[a][b] = curr && !next ? grid.at(x,y,z).type : 0;
        }
    }
}


void Mesh::emitQuad(
    const Dir& d,
    int slice,
    int a, int b,
    int w, int h,int type
) {
    float nx = 0, ny = 0, nz = 0;

    if (d.axis == X_AXIS) nx = d.sign;
    if (d.axis == Y_AXIS) ny = d.sign;
    if (d.axis == Z_AXIS) nz = d.sign;


    auto V = [&](int da, int db) -> Vertex {
        int x, y, z;
        if (d.axis == X_AXIS) {
            x = slice + (d.sign > 0);
            y = a + da;
            z = b + db;
        }
        else if (d.axis == Y_AXIS) {
            x = a + da;
            y = slice + (d.sign > 0);
            z = b + db;
        }
        else {
            x = a + da;
            y = b + db;
            z = slice + (d.sign > 0);
        }
        
        float red=0.0f, green=0, blue=0;
        
        if (type == 1) {
            red = 1.0f;
            green = 0.0f;
            blue = 0.0f;
        }
        else if (type == 2) {
            red = 1.0f;
            green = 1.0f;
            blue = 1.0f;
        }

        return {
            (float)x,(float)y,(float)z,
            nx,ny,nz,
            red,green,blue
        };

    };

    Vertex v0 = V(0, 0);
    Vertex v1 = V(h, 0);
    Vertex v2 = V(h, w);
    Vertex v3 = V(0, w);

    if (d.sign > 0) {
        vertices.push_back(v0);
        vertices.push_back(v1);
        vertices.push_back(v2);
        vertices.push_back(v0);
        vertices.push_back(v2);
        vertices.push_back(v3);
    }
    else {
        vertices.push_back(v0);
        vertices.push_back(v2);
        vertices.push_back(v1);
        vertices.push_back(v0);
        vertices.push_back(v3);
        vertices.push_back(v2);
    }
}
void Mesh::greedyMerge(
    const Dir& d,
    int slice,
    std::vector<std::vector<int>>& mask
) {
    int A = mask.size();
    int B = mask[0].size();

    for (int a = 0; a < A; a++) {
        for (int b = 0; b < B; b++) {

            if (!mask[a][b]) continue;

            int currType = mask[a][b];

            int w = 1;

            while (b + w < B && mask[a][b + w] == currType) w++;

            int h = 1;
            while (a + h < A) {
                bool ok = true;
                for (int k = 0; k < w; k++) {
                    if (!(mask[a + h][b + k] == currType)) {
                        ok = false;
                        break;
                    }
                }
                if (!ok) break;
                h++;
            }

            for (int da = 0; da < h; da++)
                for (int db = 0; db < w; db++)
                    mask[a + da][b + db] = 0;

            emitQuad(d, slice, a, b, w, h, currType);
            b += w - 1;
        }
    }
}
void Mesh::buildMesh() {

    vertices.clear();

    Dir dirs[6] = {
        {X_AXIS,-1},{X_AXIS,+1},
        {Y_AXIS,-1},{Y_AXIS,+1},
        {Z_AXIS,-1},{Z_AXIS,+1}
    };

    for (Dir d : dirs) {

        int slices, A, B;

        if (d.axis == X_AXIS) {
            slices = grid.X;
            A = grid.Y;
            B = grid.Z;
        }
        else if (d.axis == Y_AXIS) {
            slices = grid.Y;
            A = grid.X;
            B = grid.Z;
        }
        else {
            slices = grid.Z;
            A = grid.X;
            B = grid.Y;
        }

        std::vector<std::vector<int>> mask(
            A, std::vector<int>(B)
        );

        for (int s = 0; s < slices; s++) {
            buildMask(d, s, mask);
            greedyMerge(d, s, mask);
        }
    }
}



