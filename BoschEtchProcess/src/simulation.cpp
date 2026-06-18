#include "simulation.h"
#include "settings.h"
#include <cmath>
#include <random>
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <chrono>

static GLuint loadComputeProgram(const char* path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open compute shader");
    }

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

namespace Math {
    float randomFloat(float max) {
        static std::mt19937 gen{ std::random_device{}() };
        std::uniform_real_distribution<float> dist(0.0f, max);
        return dist(gen);
    }
}

Simulation::Simulation(int X_, int Y_, int Z_, float voxelSize_)
    : grid(X_, Y_, Z_) {

    X = X_;
    Y = Y_;
    Z = Z_;
    voxelSize = voxelSize_;

}


void Simulation::initRectangle(Voxel& voxel, int x0, int y0, int z0, int x1, int y1, int z1) {
    for (int x = x0; x < x1; x++)
    {
        for (int y = y0; y < y1; y++)
        {
            for (int z = z0;z < z1;z++) {

                std::vector<int> distances = {abs(x-x0), abs(x - x1 + 1), abs(y - y0), abs(y - y1 + 1), abs(z - z0), abs(z - z1 + 1) };
                auto minimum = std::min_element(distances.begin(), distances.end());

                voxel.sdf = -(*minimum);

                grid.at(x, y, z) = voxel;

            }
        }
    }
}

void Simulation::initParticle(const Particle& particle) {
    particles.push_back(particle);
}

void Simulation::tick(std::vector<float> gridData, int typesOfVoxels, int typesOfParticles)
{
    bindBuffers();



    dispatchRayMarch(rayMarchProgram, getParticleCount(), gridData, typesOfVoxels, typesOfParticles);
    dispatchHits(resolveHitsProgram);
}

void Simulation::setVoxel(int x, int y, int z, Voxel v) {
    grid.at(x, y, z) = v;
}

void Simulation::dispatchRayMarch(GLuint program, int particleCount, std::vector<float> gridData, int typesOfVoxels, int typesOfParticles)
{
    glUseProgram(program);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, reactionProbabilitiesSSBO);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(float) * gridData.size(), gridData.data());

    glUniform1f(glGetUniformLocation(program, "voxelSize"), voxelSize);
    glUniform1i(glGetUniformLocation(program, "maxSteps"), MAX_STEPS);
    glUniform3i(glGetUniformLocation(program, "gridSize"), grid.X, grid.Y, grid.Z);

    glUniform1i(glGetUniformLocation(program, "particleCount"), particleCount);
    glUniform1i(glGetUniformLocation(program, "typesOfVoxels"), typesOfVoxels);
    glUniform1i(glGetUniformLocation(program, "typesOfParticles"), typesOfParticles);
    glUniform1i(glGetUniformLocation(program, "damageRadius"), 1);

    // Reset hit counter
    {
        uint32_t zero = 0;
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, counterSSBO);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(uint32_t), &zero);
    }

    // Reset final particle counter
    {
        uint32_t zero = 0;
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, finalParticlesCount);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(uint32_t), &zero);
    }

    int groups = (particleCount + 255) / 256;
    
    glDispatchCompute(groups, 1, 1);

    glMemoryBarrier(GL_ALL_BARRIER_BITS);
}


std::vector<HitEvent> Simulation::downloadHits() {
    uint32_t hitCount = 0;

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, counterSSBO);
    glGetBufferSubData(
        GL_SHADER_STORAGE_BUFFER,
        0,
        sizeof(uint32_t),
        &hitCount
    );

    hitCount = std::min(hitCount, MAX_HITS);

    std::vector<HitEvent> hits(hitCount);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, hitSSBO);
    glGetBufferSubData(
        GL_SHADER_STORAGE_BUFFER,
        0,
        hitCount * sizeof(HitEvent),
        hits.data()
    );

    return hits;
}


void Simulation::downloadVoxels() {
    
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, voxelSSBO);
    glGetBufferSubData(
        GL_SHADER_STORAGE_BUFFER,
        0,
        Settings::X * Settings::Y * Settings::Z * sizeof(Voxel),
        grid.voxels.data()
    );

}


int Simulation::getParticleCount()
{
    uint32_t count = 0;

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, finalParticlesCount);
    glGetBufferSubData(
        GL_SHADER_STORAGE_BUFFER,
        0,
        sizeof(uint32_t),
        &count
    );

    count = std::min(count, (uint32_t)MAX_PARTICLES);

    return count;
}

void Simulation::createBuffers() {
    std::string path = "shaders/march.comp.shader";
    rayMarchProgram = loadComputeProgram(path.c_str());

    std::string path2 = "shaders/resolveHits.shader";
    resolveHitsProgram = loadComputeProgram(path2.c_str());

    std::string path3 = "shaders/particle.init.shader";
    initParticlesProgram = loadComputeProgram(path3.c_str());

    glGenBuffers(1, &particleSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, particleSSBO);
    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        sizeof(Particle) * MAX_PARTICLES,
        nullptr,
        GL_DYNAMIC_DRAW
    );

    glGenBuffers(1, &voxelSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, voxelSSBO);
    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        sizeof(Voxel) * grid.X * grid.Y * grid.Z,
        nullptr,
        GL_DYNAMIC_DRAW
    );


    glGenBuffers(1, &hitSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, hitSSBO);
    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        sizeof(HitEvent) * MAX_HITS,
        nullptr,
        GL_DYNAMIC_DRAW
    );


    glGenBuffers(1, &reactionProbabilitiesSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, reactionProbabilitiesSSBO);
    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        sizeof(float) * 100,
        nullptr,
        GL_DYNAMIC_DRAW
    );

    glGenBuffers(1, &finalParticles);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, finalParticles);
    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        sizeof(Particle) * MAX_PARTICLES,
        nullptr,
        GL_DYNAMIC_DRAW
    );

    uint32_t zero = 0;
    glGenBuffers(1, &counterSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, counterSSBO);
    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        sizeof(uint32_t),
        &zero,
        GL_DYNAMIC_DRAW
    );

    uint32_t z2 = 0;

    glGenBuffers(1, &finalParticlesCount);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, finalParticlesCount);
    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        sizeof(uint32_t),
        &z2,
        GL_DYNAMIC_DRAW
    );

}

void Simulation::bindBuffers(){
    
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, particleSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, voxelSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, hitSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 15, reactionProbabilitiesSSBO);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, counterSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 9, finalParticlesCount);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 10, finalParticles);

}

void Simulation::uploadParticles(ParticleTypeData p, int particleType) {

    constexpr float pi = 3.1415926f;
    float random = Math::randomFloat(100);
    float cosTheta = cos(p.halfAngle * 3.141592653589/180);
    glUseProgram(initParticlesProgram);

    glUniform1ui(glGetUniformLocation(initParticlesProgram, "startIndex"), getParticleCount());
    glUniform1ui(glGetUniformLocation(initParticlesProgram, "particleCount"), p.count);
    glUniform1i(glGetUniformLocation(initParticlesProgram, "type"), particleType);
    glUniform1i(glGetUniformLocation(initParticlesProgram, "deposit"), p.deposit);
    glUniform1f(glGetUniformLocation(initParticlesProgram, "energy"), p.energy);
    glUniform1f(glGetUniformLocation(initParticlesProgram, "stddev"), p.stddev);
    
    glUniform1f(glGetUniformLocation(initParticlesProgram, "cosTheta"), cosTheta);
    glUniform1f(glGetUniformLocation(initParticlesProgram, "X"), Settings::X);
    glUniform1f(glGetUniformLocation(initParticlesProgram, "Z"), Settings::Z);
    glUniform1f(glGetUniformLocation(initParticlesProgram, "seed"), random);

    glDispatchCompute((p.count + 255) / 256, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

}

void Simulation::uploadVoxels(std::vector<Voxel>& voxels) {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, voxelSSBO);
    glBufferSubData(
        GL_SHADER_STORAGE_BUFFER,
        0,
        voxels.size() * sizeof(Voxel),
        voxels.data()
    );
}

void Simulation::reset() {

    for (int x = 0; x < Settings::X; x++) {
        for (int y = 0; y < Settings::Y; y++) {
            for (int z = 0; z < Settings::Z; z++) {

                Voxel& v = grid.at(x, y, z);

                if (v.solid != 0) {
                    v.solid = 1;
                }

            }
        }
    }

}

void Simulation::dispatchHits(GLuint program) {

    uint32_t hitCount = 0;

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, counterSSBO);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(uint32_t), &hitCount);
    
    if (hitCount == 0) return;

    glUseProgram(program);
    glUniform3i(glGetUniformLocation(program, "gridSize"), grid.X, grid.Y, grid.Z);

    int groups = (hitCount + 255) / 256;

    glDispatchCompute(groups, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}
void Simulation::initSDF()
{
    int X = Settings::X;
    int Y = Settings::Y;
    int Z = Settings::Z;

    auto idx = [&](int x, int y, int z)
        {
            return x + y * X + z * X * Y;
        };

    auto inBounds = [&](int x, int y, int z)
        {
            return x >= 0 && x < X &&
                y >= 0 && y < Y &&
                z >= 0 && z < Z;
        };

    // Initialize all voxels to a large value
    for (int i = 0; i < X * Y * Z; i++)
        grid.voxels[i].sdf = 1e9f;

    int fx[6] = { 1,-1, 0, 0, 0, 0 };
    int fy[6] = { 0, 0, 1,-1, 0, 0 };
    int fz[6] = { 0, 0, 0, 0, 1,-1 };

    for (int z = 0; z < Z; z++)
    {
        for (int y = 0; y < Y; y++)
        {
            for (int x = 0; x < X; x++)
            {
                if (grid.voxels[idx(x, y, z)].solid == 0)
                    continue;

                float normalX = 0.0f;
                float normalY = 0.0f;
                float normalZ = 0.0f;

                bool isSurface = false;

                for (int f = 0; f < 6; f++)
                {
                    int nx = x + fx[f];
                    int ny = y + fy[f];
                    int nz = z + fz[f];

                    if (!inBounds(nx, ny, nz))
                    {
                        normalX += fx[f];
                        normalY += fy[f];
                        normalZ += fz[f];
                        isSurface = true;
                    }
                    else if (grid.voxels[idx(nx, ny, nz)].solid == 0)
                    {
                        normalX += fx[f];
                        normalY += fy[f];
                        normalZ += fz[f];
                        isSurface = true;
                    }
                }

                if (!isSurface)
                    continue;

                // Normalize surface normal
                float len =
                    sqrtf(
                        normalX * normalX +
                        normalY * normalY +
                        normalZ * normalZ
                    );

                if (len > 1e-6f)
                {
                    normalX /= len;
                    normalY /= len;
                    normalZ /= len;
                }

                // Build narrow-band SDF (radius = 5 voxels)
                for (int dz = -5; dz <= 5; dz++)
                {
                    for (int dy = -5; dy <= 5; dy++)
                    {
                        for (int dx = -5; dx <= 5; dx++)
                        {
                            int nx = x + dx;
                            int ny = y + dy;
                            int nz = z + dz;

                            if (!inBounds(nx, ny, nz))
                                continue;

                            float dist =
                                sqrtf(
                                    float(
                                        dx * dx +
                                        dy * dy +
                                        dz * dz
                                        )
                                );

                            if (dist > 5.0f)
                                continue;

                            float dot = 0.0f;

                            if (dist > 1e-6f)
                            {
                                float vx = dx / dist;
                                float vy = dy / dist;
                                float vz = dz / dist;

                                dot =
                                    vx * normalX +
                                    vy * normalY +
                                    vz * normalZ;
                            }

                            float signedDist =
                                (dot <= 0.0f)
                                ? -dist
                                : dist;

                            int nidx = idx(nx, ny, nz);

                            if (fabsf(signedDist) <
                                fabsf(grid.voxels[nidx].sdf))
                            {
                                grid.voxels[nidx].sdf = signedDist;
                            }
                        }
                    }
                }
            }
        }
    }
}