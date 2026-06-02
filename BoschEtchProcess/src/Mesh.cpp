#include "Mesh.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
// ---------- helper ----------
static GLuint loadComputeProgram(const char* path) {
    std::ifstream file(path);
    std::stringstream ss;
    ss << file.rdbuf();
    std::string src = ss.str();
    const char* csrc = src.c_str();

    GLuint cs = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(cs, 1, &csrc, nullptr);
    glCompileShader(cs);

    GLint ok;
    glGetShaderiv(cs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(cs, 1024, nullptr, log);
        std::cerr << "COMPUTE SHADER ERROR:\n" << log << std::endl;
        std::abort();
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, cs);
    glLinkProgram(prog);

    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(prog, 1024, nullptr, log);
        throw std::runtime_error(log);
    }

    glDeleteShader(cs);
    return prog;
}

// ---------- Mesh ----------
Mesh::Mesh(Grid& g) : grid(g) {}

Mesh::~Mesh() {
    glDeleteBuffers(1, &voxelSSBO);
    glDeleteBuffers(1, &vertexSSBO);
    glDeleteBuffers(1, &counterSSBO);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(computeProgram);
}

void Mesh::setRenderingProgram(GLuint shaderProgram) {
    renderProgram = shaderProgram;
}

void Mesh::setVoxelBuffer(GLuint ssbo){
    voxelSSBO = ssbo;
}

std::vector<int> Mesh::extractSlice(
    int dir,
    int sliceIndex
)
{
    glUseProgram(sliceProgram);

    glUniform3i(
        glGetUniformLocation(sliceProgram, "gridSize"),
        grid.X, grid.Y, grid.Z
    );

    glUniform1i(
        glGetUniformLocation(sliceProgram, "plane"),
        dir
    );

    glUniform1i(
        glGetUniformLocation(sliceProgram, "sliceIndex"),
        sliceIndex
    );

    int width = 0;
    int height = 0;

    switch (dir)
    {
        case 0:
            width = grid.X;
            height = grid.Y;
            break;

        case 1:
            width = grid.X;
            height = grid.Z;
            break;

        case 2:
            width = grid.Y;
            height = grid.Z;
            break;
    }

    glDispatchCompute(
        (width + 15) / 16,
        (height + 15) / 16,
        1
    );

    glMemoryBarrier(
        GL_SHADER_STORAGE_BARRIER_BIT
    );

    std::vector<int> slice(width * height);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, sliceSSBO);
    glGetBufferSubData(
        GL_SHADER_STORAGE_BUFFER,
        0,
        slice.size() * sizeof(int),
        slice.data()
    );
    return slice;
}
void Mesh::initGPU() {
    const size_t MAX_VERTS = grid.X * grid.Y * grid.Z;

    // vertex SSBO
    glGenBuffers(1, &vertexSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, vertexSSBO);
    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        MAX_VERTS * sizeof(Vertex),
        nullptr,
        GL_DYNAMIC_DRAW
    );

    glGenBuffers(1, &sliceSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, sliceSSBO);
    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        1000 * 1000 * sizeof(int),
        nullptr,
        GL_DYNAMIC_DRAW
    );


    // atomic counter
    glGenBuffers(1, &counterSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, counterSSBO);
    uint32_t zero = 0;
    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        sizeof(uint32_t),
        &zero,
        GL_DYNAMIC_DRAW
    );

    glGenBuffers(1, &voxelCountSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, voxelCountSSBO);
    uint32_t zero2 = 0;
    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        sizeof(uint32_t),
        &zero2,
        GL_DYNAMIC_DRAW
    );

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, voxelSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 14, voxelCountSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, vertexSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, counterSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 13, sliceSSBO);

    // VAO
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vertexSSBO);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
        sizeof(Vertex), (void*)(4 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE,
        sizeof(Vertex), (void*)(8 * sizeof(float)));
    glEnableVertexAttribArray(2);

    // shaders
    std::string path = "shaders/mesh.comp.shader";
    computeProgram = loadComputeProgram(path.c_str());

    path = "shaders/axes.shader";
    axesProgram = loadComputeProgram(path.c_str());

    path = "shaders/measure.shader";
    sliceProgram = loadComputeProgram(path.c_str());

}
void Mesh::buildMesh() {
    vertCount = 0;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, counterSSBO);
    uint32_t zero = 0;
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(uint32_t), &zero);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, voxelCountSSBO);
    uint32_t zero2 = 0;
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(uint32_t), &zero2);

    
        glUseProgram(computeProgram);
        glUniform3i(
            glGetUniformLocation(computeProgram, "gridSize"),
            grid.X, grid.Y, grid.Z
        );

        glDispatchCompute(
            (grid.X + 7) / 8,
            (grid.Y + 7) / 8,
            (grid.Z + 7) / 8
        );


        glMemoryBarrier(
            GL_SHADER_STORAGE_BARRIER_BIT |
            GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT
        );



    glUseProgram(axesProgram);
    glUniform3i(
        glGetUniformLocation(axesProgram, "gridSize"),
        grid.X/10, grid.Y, grid.Z/10
    );
    glUniform1i(
        glGetUniformLocation(axesProgram, "size"),
        20
    );
    glDispatchCompute(
        (grid.X + 7) / 8,
        (1 + 7) / 8,
        (grid.Z + 7) / 8
    );


    glMemoryBarrier(
        GL_SHADER_STORAGE_BARRIER_BIT |
        GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT
    );

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, counterSSBO);
    glGetBufferSubData(
        GL_SHADER_STORAGE_BUFFER,
        0,
        sizeof(uint32_t),
        &vertCount
    );
    
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, voxelCountSSBO);
    glGetBufferSubData(
        GL_SHADER_STORAGE_BUFFER,
        0,
        sizeof(uint32_t),
        &voxelCount
    );

}

void Mesh::draw() {
    glUseProgram(renderProgram);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, vertCount);
}
