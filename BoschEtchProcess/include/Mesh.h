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
    void buildMesh();   // dispatch compute
    void draw();        // render
    void setRenderingProgram(GLuint program);
    void setVoxelBuffer(GLuint ssbo);
    uint32_t vertCount = 0;

private:
    Grid& grid;

    GLuint voxelSSBO = 0;
    GLuint vertexSSBO = 0;
    GLuint counterSSBO = 0;
    GLuint vao = 0;
    GLuint computeProgram = 0;
    GLuint renderProgram = 0;

};