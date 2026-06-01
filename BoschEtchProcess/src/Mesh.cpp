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
    glDeleteBuffers(1, &chunkSSBO);
    glDeleteBuffers(1, &vertexSSBO);
    glDeleteBuffers(1, &counterSSBO);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(computeProgram);
}

void Mesh::setRenderingProgram(GLuint shaderProgram) {
    renderProgram = shaderProgram;
}

void Mesh::setSSBO(GLuint ssbo, GLuint ssbo2){
    dirtyIndicesSSBO = ssbo;
    chunkSSBO = ssbo2;
}

void Mesh::initGPU() {
    const size_t MAX_VERTS = grid.X * grid.Y * grid.Z * 6;
    numChunkX = (grid.X + chunkSize - 1) / chunkSize;
    numChunkY = (grid.Y + chunkSize - 1) / chunkSize;
    numChunkZ = (grid.Z + chunkSize - 1) / chunkSize;

    // vertex SSBO
    glGenBuffers(1, &vertexSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, vertexSSBO);
    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        MAX_VERTS * sizeof(Vertex),
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

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 14, voxelCountSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, vertexSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, counterSSBO);

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

    path = "shaders/resetChunks.shader";
    resetChunkProgram = loadComputeProgram(path.c_str());

    path = "shaders/allocateCount.shader";
    allocateCountProgram = loadComputeProgram(path.c_str());

}


void Mesh::resetChunks() {
    glUseProgram(resetChunkProgram);
    glUniform3i(glGetUniformLocation(resetChunkProgram, "chunkGridSize"), numChunkX, numChunkY, numChunkZ);
    glDispatchCompute((numChunkX + 7) / 8, (numChunkY + 7) / 8, (numChunkZ + 7) / 8);
    glMemoryBarrier(GL_ALL_BARRIER_BITS);
}
void Mesh::allocateCount(int dirtyCount) {

    glUseProgram(allocateCountProgram);

    glUniform1i(
        glGetUniformLocation(
            allocateCountProgram,
            "dirtyCount"
        ), dirtyCount
    );
    glUniform3i(
        glGetUniformLocation(
            allocateCountProgram,
            "gridSize"
        ),
        grid.X,
        grid.Y,
        grid.Z
    );

    glUniform3i(
        glGetUniformLocation(
            allocateCountProgram,
            "chunkGridSize"
        ),
        numChunkX,
        numChunkY,
        numChunkZ
    );

    glDispatchCompute(
        numChunkX * numChunkY * numChunkZ * 4,
        4,
        4
    );

    glMemoryBarrier(
        GL_SHADER_STORAGE_BARRIER_BIT |
        GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT
    );
}

void Mesh::buildMesh(int dirtyCount)
{

    allocateCount(dirtyCount);

    vertCount = 0;

    uint32_t zero = 0;

    glBindBuffer(
        GL_SHADER_STORAGE_BUFFER,
        counterSSBO
    );

    glBufferSubData(
        GL_SHADER_STORAGE_BUFFER,
        0,
        sizeof(uint32_t),
        &zero
    );

    glUseProgram(computeProgram);

    glUniform1i(
        glGetUniformLocation(
            computeProgram,
            "dirtyCount"
        ), dirtyCount
    );
    glUniform3i(
        glGetUniformLocation(
            computeProgram,
            "gridSize"
        ),
        grid.X,
        grid.Y,
        grid.Z
    );

    glUniform3i(
        glGetUniformLocation(
            computeProgram,
            "chunkGridSize"
        ),
        numChunkX,
        numChunkY,
        numChunkZ
    );

    glDispatchCompute(
        dirtyCount * 4,
        4,
        4
    );

    glMemoryBarrier(
        GL_SHADER_STORAGE_BARRIER_BIT |
        GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT
    );

    glUseProgram(axesProgram);
    glUniform3i(
        glGetUniformLocation(axesProgram, "gridSize"),
        grid.X / 10, grid.Y, grid.Z / 10
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

    glBindBuffer(
        GL_SHADER_STORAGE_BUFFER,
        counterSSBO
    );

    glGetBufferSubData(
        GL_SHADER_STORAGE_BUFFER,
        0,
        sizeof(uint32_t),
        &vertCount
    );
}

void Mesh::draw(int dirtyCount)
{
    struct DrawInfo
    {
        int vertexOffset;
        int vertexCount;
    };
    glUseProgram(renderProgram);
    glBindVertexArray(vao);

    // Download dirty indices
    std::vector<uint32_t> dirty(dirtyCount);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, dirtyIndicesSSBO);
    glGetBufferSubData(
        GL_SHADER_STORAGE_BUFFER,
        0,
        dirtyCount * sizeof(uint32_t),
        dirty.data()
    );

    // Download only the draw ranges for dirty chunks
    std::vector<DrawInfo> draws(dirtyCount);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunkSSBO);

    
    for (int i = 0; i < dirtyCount; i++)
    {
        const GLintptr offset =
            dirty[i] * sizeof(Chunk) +
            offsetof(Chunk, vertexOffset);

        glGetBufferSubData(
            GL_SHADER_STORAGE_BUFFER,
            offset,
            sizeof(DrawInfo),
            &draws[i]
        );
    }

    for (const DrawInfo& d : draws)
    {
        if (d.vertexCount > 0)
        {
            glDrawArrays(
                GL_TRIANGLES,
                d.vertexOffset,
                d.vertexCount
            );
        }
    }
}