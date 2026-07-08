#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "simulation.h"
#include "settings.h"

struct VoxelMaterialInfo
{
    int type;
    std::string name;
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

Simulation stackSimulation();
const std::vector<VoxelMaterialInfo>& stackSimulationMaterials();
