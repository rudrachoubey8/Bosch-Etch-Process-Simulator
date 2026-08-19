#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include "ChemicalReactions.h"
#include "imgui.h"
#include "stackSimulations.h"
#include "structures.h"

bool writeParticleType(std::ostream& out, const ParticleTypeData& particle);
bool readParticleType(std::istream& in, ParticleTypeData& particle, uint32_t version);
void writeMaterialData(std::ostream& out);
void tryReadMaterialData(std::istream& in);

int probabilityIndex(
    int particle,
    int voxelType,
    int probabilityType,
    int particleCount,
    int voxelTypeCount);
void setDefaultProbabilities(
    std::vector<float>& probabilities,
    int particleCount,
    int voxelTypeCount);
void rebuildProbabilityGrid(
    const std::vector<ParticleTypeData>& oldParticles,
    const std::vector<float>& oldGrid,
    const std::vector<ParticleTypeData>& newParticles,
    int voxelTypeCount,
    std::vector<float>& newGrid);

ParticleTypeData makeSprayParticle(
    const std::string& name,
    const SpeciesProperties& properties,
    double density);
void makeMonoEnergy(ParticleTypeData& particle);
ParticleTypeData makeFireableFromTemplate(
    const ParticleTypeData& source,
    int interval,
    int releaseDuration);
ParticleTypeData makeCustomParticle(
    const std::string& name,
    const SpeciesProperties& properties,
    int releaseDuration);

const VoxelMaterialInfo* findStackMaterial(int type);
ImVec4 materialColor(const VoxelMaterialInfo& material);
const VoxelMaterialInfo* materialByListIndex(int index);
int materialListIndexForType(int type);
void applyMaterialPropertiesToVoxels(std::vector<Voxel>& voxels);

struct IedfSweepCurve
{
    std::string label;
    EnergyDistribution distribution;
};

const ParticleTypeData* findParticleByName(
    const std::vector<ParticleTypeData>& particles,
    const std::string& name);
std::vector<ParticleTypeData> runPlasmaCase(
    const BulkModel& controls,
    double absorbedPower,
    double biasPower);
void buildIedfSweep(
    const BulkModel& controls,
    const std::string& speciesName,
    const std::vector<double>& values,
    bool varyBias,
    std::vector<IedfSweepCurve>& curves);
void plotIedfCurves(
    const char* title,
    const std::vector<IedfSweepCurve>& curves);

uint32_t rgbaPixel(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
uint32_t materialPixel(int type);
bool writeIedfCsv(const char* path, const ParticleTypeData& particle);
bool writeSliceCsv(
    const char* path,
    int width,
    int height,
    const std::vector<int>& slice);
bool writeSliceImage(
    const char* path,
    int width,
    int height,
    const std::vector<uint32_t>& pixels);
int maxSliceIndexForDirection(int sliceDir);
