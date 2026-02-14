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


Simulation::Simulation(int X_, int Y_, int Z_, float voxelSize_)
    : grid(X_, Y_, Z_) {

    X = X_;
    Y = Y_;
    Z = Z_;
    voxelSize = voxelSize_;

}


void Simulation::initRectangle(const Voxel& voxel, int x0, int y0, int z0, int x1, int y1, int z1) {
    for (int x = x0; x < x1; x++)
    {
        for (int y = y0; y < y1; y++)
        {
            for (int z = z0;z < z1;z++) {
                grid.at(x, y, z) = voxel;
            }
        }
    }
}

void Simulation::initParticle(const Particle& particle) {
    particles.push_back(particle);
}

void Simulation::tick(float dt) {
    int SUBSTEPS = 1;
    float subDt = dt / SUBSTEPS;
    bindBuffers();

    for (int i = 0; i < SUBSTEPS; ++i) {
        uploadParticles(particles);
        uploadVoxels(grid.voxels);

        dispatchRayMarch(rayMarchProgram, particles.size());

        auto hits = downloadHits();
        resolveHitEvents(hits);
    }

    particles = downloadParticles();
    
}

void Simulation::dispatchRayMarch(GLuint program, int particleCount)
{
    glUseProgram(program);

    glUniform1f(glGetUniformLocation(program, "voxelSize"), voxelSize);
    glUniform1i(glGetUniformLocation(program, "maxSteps"), MAX_STEPS);
    glUniform3i(glGetUniformLocation(program, "gridSize"), grid.X, grid.Y, grid.Z);
    glUniform1i(glGetUniformLocation(program, "particleCount"), particleCount);

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

std::vector<Particle> Simulation::downloadParticles()
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

    std::vector<Particle> gpuParticles(count);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, finalParticles);
    glGetBufferSubData(
        GL_SHADER_STORAGE_BUFFER,
        0,
        count * sizeof(Particle),
        gpuParticles.data()
    );

    return gpuParticles;
}




void Simulation::createBuffers() {

    std::string path = std::string(PROJECT_ROOT) + "/shaders/march.comp.shader";
    rayMarchProgram = loadComputeProgram(path.c_str());

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

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, counterSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 9, finalParticlesCount);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 10, finalParticles);

}

void Simulation::uploadParticles(std::vector<Particle>& particles) {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, particleSSBO);
    glBufferSubData(
        GL_SHADER_STORAGE_BUFFER,
        0,
        particles.size() * sizeof(Particle),
        particles.data()
    );
}

void Simulation::uploadVoxels(std::vector<Voxel>& voxels) {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, voxelSSBO);
    glBufferSubData(
        GL_SHADER_STORAGE_BUFFER,
        0,
        grid.voxels.size() * sizeof(Voxel),
        grid.voxels.data()
    );
}



void Simulation::resolveHitEvents(std::vector<HitEvent>& hits) {
    Voxel voxel;
    voxel.type = 2;
    voxel.threshold = 1000;
    voxel.solid = 1;
    voxel.depositThreshold = 10000;
    voxel.voxelSize = 1.0f;

    for (auto& h : hits) {
        //std::cout << h.damage;
        if (!grid.inBounds(h.cx, h.cy, h.cz)) continue;

        Voxel& v = grid.at(h.cx, h.cy, h.cz);

        if (!v.solid) continue;

        

        if (h.flags & 1) {
            v.depositThreshold -= h.damage;

            if (v.depositThreshold <= 0.0f) {
                v = voxel;
                v.depositThreshold = 10.0f;
            }
        }

        else {
            v.threshold -= h.damage;

            if (v.threshold <= 0.0f) {
                v.solid = 0;
                v.type = 0;
            }
        }
    }
}
