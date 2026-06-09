#pragma once
#include <vector>
#include "glad/glad.h"
#include <GLFW/glfw3.h>

#include "structures.h"
#include <shader.h>

using namespace std;
class Mesh {
public:
    Mesh(Grid& g);
    ~Mesh();

    void initGPU();     // create buffers, shaders, VAO
    void buildMesh(float rayOrigin[3], float viewMatrix[9]);   // dispatch compute
    void draw();        // render
    void setRenderingProgram(GLuint program);
    void setVoxelBuffer(GLuint ssbo);
    std::vector<int> extractSlice(int dir, int sliceIndex);

    int voxelCount = 0;

private:

    int width = 1920;
    int height = 1080;

    Grid& grid;

    GLuint voxelSSBO = 0;
    GLuint sliceSSBO = 0;
    GLuint screenTexture;

    GLuint vao = 0;
    GLuint computeProgram = 0;
    GLuint renderProgram = 0;
    GLuint sliceProgram = 0;
    GLuint voxelCountSSBO = 0;

};