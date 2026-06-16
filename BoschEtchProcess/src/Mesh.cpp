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

    glGenVertexArrays(1, &vao);
    glGenFramebuffers(1, &fbo);

    glGenTextures(1, &screenTexture);
    glBindTexture(GL_TEXTURE_2D, screenTexture);

    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, width, height);

    glGenBuffers(1, &sliceSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, sliceSSBO);
    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        1000 * 1000 * sizeof(int),
        nullptr,
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

    glBindImageTexture(
        4,
        screenTexture,
        0,
        GL_FALSE,
        0,
        GL_READ_WRITE,
        GL_RGBA8
    );

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 14, voxelCountSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 13, sliceSSBO);

    // shaders
    std::string path = "shaders/mesh.comp.shader";
    computeProgram = loadComputeProgram(path.c_str());
    
    path = "shaders/measure.shader";
    sliceProgram = loadComputeProgram(path.c_str());

}
void Mesh::buildMesh(float rayOrigin[3], float viewMatrix[9], int sliceDir, int sliceIndex) {


    glBindBuffer(GL_SHADER_STORAGE_BUFFER, voxelCountSSBO);
    uint32_t zero2 = 0;
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(uint32_t), &zero2);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, screenTexture, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glUseProgram(computeProgram);

    glUniform3i(
        glGetUniformLocation(computeProgram, "gridSize"),
        grid.X, grid.Y, grid.Z
    );


    glUniform3f(
        glGetUniformLocation(computeProgram, "bounds"),
        grid.X/200.0f, grid.Y/200.0f, grid.Z/200.0f
    );

    glUniform3f(
        glGetUniformLocation(computeProgram, "center"),
        0,0,0
    );

    glUniform3f(
        glGetUniformLocation(computeProgram, "rayOrigin"),
        rayOrigin[0], rayOrigin[1], rayOrigin[2]
    );

    glUniformMatrix3fv(
        glGetUniformLocation(computeProgram, "viewMatrix"),
        1, GL_TRUE, viewMatrix
    );


    glUniform2i(
        glGetUniformLocation(computeProgram, "slice"),
        sliceDir, sliceIndex
    );

    // Project cuboid corners to screen space and find 2D AABB
    float halfX = (grid.X / 300.0f) * 0.5f;
    float halfY = (grid.Y / 300.0f) * 0.5f;
    float halfZ = (grid.Z / 300.0f) * 0.5f;

    // Center is (0,0,0) so corners are just ±half extents
    float corners[8][3] = {
        {-halfX, -halfY, -halfZ}, { halfX, -halfY, -halfZ},
        {-halfX,  halfY, -halfZ}, { halfX,  halfY, -halfZ},
        {-halfX, -halfY,  halfZ}, { halfX, -halfY,  halfZ},
        {-halfX,  halfY,  halfZ}, { halfX,  halfY,  halfZ},
    };

    float minSX = 1e9f, minSY = 1e9f;
    float maxSX = -1e9f, maxSY = -1e9f;

    for (auto& c : corners) {
        // Transform by viewMatrix (row-major, so multiply row-vector * matrix)
        float vx = viewMatrix[0] * c[0] + viewMatrix[1] * c[1] + viewMatrix[2] * c[2];
        float vy = viewMatrix[3] * c[0] + viewMatrix[4] * c[1] + viewMatrix[5] * c[2];
        float vz = viewMatrix[6] * c[0] + viewMatrix[7] * c[1] + viewMatrix[8] * c[2];

        // vz here is depth along view direction; if using a pinhole-style
        // projection you need focal length — adjust focalLen to match your shader
        float focalLen = 1.0f; // same value your shader uses
        if (vz <= 0.001f) { minSX = -1; minSY = -1; maxSX = 2; maxSY = 2; break; }

        float sx = (vx / vz) * focalLen;  // NDC-ish [-1, 1]
        float sy = (vy / vz) * focalLen;

        minSX = std::min(minSX, sx); maxSX = std::max(maxSX, sx);
        minSY = std::min(minSY, sy); maxSY = std::max(maxSY, sy);
    }

    // Convert NDC to pixel coords, clamp to screen
    int pxMin = std::max(0, (int)std::floor((minSX * 0.5f + 0.5f) * width));
    int pyMin = std::max(0, (int)std::floor((minSY * 0.5f + 0.5f) * height));
    int pxMax = std::min(width, (int)std::ceil((maxSX * 0.5f + 0.5f) * width));
    int pyMax = std::min(height, (int)std::ceil((maxSY * 0.5f + 0.5f) * height));

    int tileW = pxMax - pxMin;
    int tileH = pyMax - pyMin;
    if (tileW <= 0 || tileH <= 0) return; // cuboid off screen

    // Pass tile origin so shader reconstructs correct pixel coords
    glUniform2i(glGetUniformLocation(computeProgram, "tileOffset"), pxMin, pyMin);

    glDispatchCompute(
        (tileW + 15) / 16,
        (tileH + 15) / 16,
        1
    );
    glMemoryBarrier(
        GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
        GL_TEXTURE_FETCH_BARRIER_BIT
    );


}
void Mesh::draw()
{
    glUseProgram(renderProgram);

    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, screenTexture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}