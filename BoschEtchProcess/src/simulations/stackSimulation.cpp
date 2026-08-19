
#include "stackSimulations.h"

std::vector<VoxelMaterialInfo>& stackSimulationMaterials()
{
    static std::vector<VoxelMaterialInfo> materials = {
        {0, "SiO2", 255, 105, 0, 1000.0f, 70.0f, 1},
        {1, "Si3N4", 0, 255, 0, 40.0f, 70.0f, 1},
        {2, "Si", 128, 0, 128, 50.0f, 70.0f, 1},
        {3, "DX", 0, 255, 255, 10000.0f, 10000.0f, 1}
    };
    return materials;
}

static Voxel voxelFromMaterial(const VoxelMaterialInfo& material)
{
    Voxel voxel;
    voxel.solid = material.solid;
    voxel.threshold = material.threshold;
    voxel.depositThreshold = material.depositThreshold;
    voxel.voxelSize = Settings::voxelSize;
    voxel.type = material.type;
    return voxel;
}

Simulation stackSimulation() {
    Simulation simulation(
        Settings::X,
        Settings::Y,
        Settings::Z,
        Settings::voxelSize
    );

    std::vector<VoxelMaterialInfo>& materials = stackSimulationMaterials();
    Voxel SiO2 = voxelFromMaterial(materials[0]);
    Voxel Si3N4 = voxelFromMaterial(materials[1]);
    Voxel Si = voxelFromMaterial(materials[2]);


    // Bottom oxide
    simulation.initRectangle(
        SiO2,
        0, 0, 0,
        Settings::X, 20, Settings::Z
    );

    // Alternating layers
    for (int i = 1; i <= 10; i++)
    {
        if (i % 2 == 0)
        {
            simulation.initRectangle(
                Si3N4,
                0, i * 20, 0,
                Settings::X, (i + 1) * 20, Settings::Z
            );
        }
        else
        {
            simulation.initRectangle(
                Si,
                0, i * 20, 0,
                Settings::X, (i + 1) * 20, Settings::Z
            );
        }
    }

    // Top oxide
    simulation.initRectangle(
        SiO2,
        0, 220, 0,
        Settings::X, 280, Settings::Z
    );


    // ----------------------------------------------------
    // Create a cylindrical hole (diameter 24, radius 12)
    // through the top oxide layer
    // ----------------------------------------------------

    const int radius = 100;

    const int centerX = Settings::X / 2;
    const int centerZ = Settings::Z / 2;

    for (int x = centerX - radius; x <= centerX + radius; x++)
    {
        for (int z = centerZ - radius; z <= centerZ + radius; z++)
        {
            int dx = x - centerX;
            int dz = z - centerZ;

            if (dx * dx + dz * dz <= radius * radius)
            {
                for (int y = 220; y < 280; y++)
                {
                    simulation.grid.voxels[
                        x +
                            y * Settings::X +
                            z * Settings::X * Settings::Y
                    ].solid = 0;
                }
            }
        }
    }

    return simulation;
}
