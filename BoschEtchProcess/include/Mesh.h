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
    void buildMesh(int dirtyCount);   // dispatch compute
    void draw(int dirtyCount);        // render
    void resetChunks();
    void setRenderingProgram(GLuint program);
    void setSSBO(GLuint dirtyIndicesSSBO, GLuint chunkSSBO);
    uint32_t vertCount = 0;
    int voxelCount = 0;
    int numChunkX, numChunkY, numChunkZ;

private:
    Grid& grid;

    GLuint chunkSSBO = 0;

    GLuint vertexSSBO = 0;
    GLuint counterSSBO = 0;
    
    GLuint dirtyIndicesSSBO = 0;
    GLuint dirtyCountSSBO = 0;

    GLuint vao = 0;



    GLuint computeProgram = 0;
    GLuint axesProgram = 0;
    GLuint renderProgram = 0;
    GLuint resetChunkProgram = 0;
    GLuint voxelCountSSBO = 0;

};