#include "simulation.h"
#include "settings.h"
#include "stackSimulations.h"
#include <algorithm>
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

void Simulation::tick(const std::vector<float>& gridData, int typesOfVoxels, int typesOfParticles)
{
    bindBuffers();



    dispatchRayMarch(rayMarchProgram, getParticleCount(), gridData, typesOfVoxels, typesOfParticles);
    dispatchHits(resolveHitsProgram);
}

void Simulation::setVoxel(int x, int y, int z, Voxel v) {
    grid.at(x, y, z) = v;
}

void Simulation::dispatchRayMarch(GLuint program, int particleCount, const std::vector<float>& gridData, int typesOfVoxels, int typesOfParticles)
{
    glUseProgram(program);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, reactionProbabilitiesSSBO);
    if (gridData.size() > reactionProbabilitiesCapacity)
    {
        glBufferData(
            GL_SHADER_STORAGE_BUFFER,
            sizeof(float) * gridData.size(),
            gridData.data(),
            GL_DYNAMIC_DRAW);
        reactionProbabilitiesCapacity = gridData.size();
    }
    else if (!gridData.empty())
    {
        glBufferSubData(
            GL_SHADER_STORAGE_BUFFER,
            0,
            sizeof(float) * gridData.size(),
            gridData.data());
    }

    glUniform1f(glGetUniformLocation(program, "voxelSize"), voxelSize);
    glUniform1i(glGetUniformLocation(program, "maxSteps"), MAX_STEPS);
    glUniform3i(glGetUniformLocation(program, "gridSize"), grid.X, grid.Y, grid.Z);

    glUniform1i(glGetUniformLocation(program, "particleCount"), particleCount);
    glUniform1i(glGetUniformLocation(program, "typesOfVoxels"), typesOfVoxels);
    glUniform1i(glGetUniformLocation(program, "typesOfParticles"), typesOfParticles);

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

    if (particleCount <= 0)
        return;

    int groups = (particleCount + 255) / 256;
    
    glDispatchCompute(groups, 1, 1);

    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    uint32_t survivorCount = 0;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, finalParticlesCount);
    glGetBufferSubData(
        GL_SHADER_STORAGE_BUFFER,
        0,
        sizeof(uint32_t),
        &survivorCount);

    survivorCount = std::min(survivorCount, MAX_PARTICLES);
    if (survivorCount > 0)
    {
        glBindBuffer(GL_COPY_READ_BUFFER, finalParticles);
        glBindBuffer(GL_COPY_WRITE_BUFFER, particleSSBO);
        glCopyBufferSubData(
            GL_COPY_READ_BUFFER,
            GL_COPY_WRITE_BUFFER,
            0,
            0,
            sizeof(Particle) * survivorCount);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }
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
        grid.voxels.size() * sizeof(Voxel),
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
    reactionProbabilitiesCapacity = 100;
    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        sizeof(float) * reactionProbabilitiesCapacity,
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

    glGenBuffers(1, &iedfSSBO);

}

void Simulation::bindBuffers(){
    
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, particleSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, voxelSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, hitSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 15, reactionProbabilitiesSSBO);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, counterSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 9, finalParticlesCount);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 10, finalParticles);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 11, iedfSSBO);


}

void Simulation::uploadParticles(
    ParticleTypeData p,
    int particleType)
{
    constexpr float PI = 3.14159265358979323846f;

    float random = Math::randomFloat(100.0f);

    float cosTheta =
        cosf(p.halfAngle * PI / 180.0f);

    // Build CDF
    std::vector<EnergyBin> bins;

    float cumulative = 0.0f;
    
    if (p.iedf.energyCenters.size() != p.iedf.pdf.size())
        return;

    if (p.iedf.pdf.empty())
    {
        p.iedf.energyCenters = { std::max(p.energy, 0.01f) };
        p.iedf.pdf = { 1.0f };
    }

    double totalWeight = 0.0;
    std::vector<double> weights(p.iedf.pdf.size(), 0.0);
    for (int i = 0; i < p.iedf.pdf.size(); i++)
    {
        weights[i] = std::max(static_cast<double>(p.iedf.pdf[i]), 0.0);
        totalWeight += weights[i];
    }

    if (totalWeight <= 0.0 || !std::isfinite(totalWeight))
    {
        p.iedf.energyCenters = { std::max(p.energy, 0.01f) };
        p.iedf.pdf = { 1.0f };
        weights.assign(1, 1.0);
        totalWeight = 1.0;
    }

    for (int i = 0; i < p.iedf.pdf.size(); i++)
    {
        cumulative += static_cast<float>(weights[i] / totalWeight);

        bins.push_back(
            {
                p.iedf.energyCenters[i],
                cumulative
            });
    }

    if (!bins.empty())
        bins.back().cdf = 1.0f;

    const uint32_t startIndex = static_cast<uint32_t>(getParticleCount());
    const uint32_t available =
        startIndex < MAX_PARTICLES ? MAX_PARTICLES - startIndex : 0;
    const uint32_t particleCount = std::min(
        static_cast<uint32_t>(std::max(p.count, 0)),
        available);

    if (bins.empty() || particleCount == 0)
        return;

    // Upload IEDF
    glBindBuffer(
        GL_SHADER_STORAGE_BUFFER,
        iedfSSBO);

    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        bins.size() * sizeof(EnergyBin),
        bins.data(),
        GL_DYNAMIC_DRAW);
    glBindBufferBase(
        GL_SHADER_STORAGE_BUFFER,
        11,
        iedfSSBO);

    // Init shader uniforms
    glUseProgram(initParticlesProgram);

    glUniform1ui(
        glGetUniformLocation(
            initParticlesProgram,
            "startIndex"),
        startIndex);

    glUniform1ui(
        glGetUniformLocation(
            initParticlesProgram,
            "particleCount"),
        particleCount);

    glUniform1i(
        glGetUniformLocation(
            initParticlesProgram,
            "deposit"),
        p.deposit);

    glUniform1i(
        glGetUniformLocation(
            initParticlesProgram,
            "depositVoxelType"),
        p.depositVoxelType);

    glUniform1i(
        glGetUniformLocation(
            initParticlesProgram,
            "type"),
        particleType);

    glUniform1f(
        glGetUniformLocation(
            initParticlesProgram,
            "cosTheta"),
        cosTheta);

    glUniform1f(
        glGetUniformLocation(
            initParticlesProgram,
            "X"),
        static_cast<float>(grid.X));

    glUniform1f(
        glGetUniformLocation(
            initParticlesProgram,
            "spawnY"),
        static_cast<float>(grid.Y - 10));

    glUniform1f(
        glGetUniformLocation(
            initParticlesProgram,
            "Z"),
        static_cast<float>(grid.Z));

    glUniform1f(
        glGetUniformLocation(
            initParticlesProgram,
            "seed"),
        random);

    glUniform1i(
        glGetUniformLocation(
            initParticlesProgram,
            "nBins"),
        bins.size());

    glUniform1f(
        glGetUniformLocation(
            initParticlesProgram,
            "mass"),
        Species[p.name].mass);
    
    // Launch
    glDispatchCompute(
        (particleCount + 255) / 256,
        1,
        1);

    glMemoryBarrier(
        GL_SHADER_STORAGE_BARRIER_BIT);

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

    for (int x = 0; x < grid.X; x++) {
        for (int y = 0; y < grid.Y; y++) {
            for (int z = 0; z < grid.Z; z++) {

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

    std::vector<float> voxelThresholds(16, 10000.0f);
    std::vector<float> voxelDepositThresholds(16, 10000.0f);
    int voxelTypeCount = 0;
    for (const VoxelMaterialInfo& material : stackSimulationMaterials())
    {
        if (material.type < 0 || material.type >= 16)
            continue;

        voxelThresholds[material.type] = material.threshold;
        voxelDepositThresholds[material.type] = material.depositThreshold;
        voxelTypeCount = std::max(voxelTypeCount, material.type + 1);
    }

    glUniform1i(glGetUniformLocation(program, "voxelTypeCount"), voxelTypeCount);
    glUniform1fv(glGetUniformLocation(program, "voxelThresholds"), 16, voxelThresholds.data());
    glUniform1fv(glGetUniformLocation(program, "voxelDepositThresholds"), 16, voxelDepositThresholds.data());
    glUniform1f(glGetUniformLocation(program, "sputterYieldScale"), SPUTTER_YIELD_SCALE);
    glUniform1f(glGetUniformLocation(program, "sputterEnergyDamping"), SPUTTER_ENERGY_DAMPING);
    glUniform1f(glGetUniformLocation(program, "minSputterEnergy"), MIN_SPUTTER_ENERGY);

    hitCount = std::min(hitCount, MAX_HITS);
    int groups = (hitCount + 255) / 256;

    glDispatchCompute(groups, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}
