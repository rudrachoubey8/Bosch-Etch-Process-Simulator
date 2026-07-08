
#include "stackSimulations.h"

const std::vector<VoxelMaterialInfo>& stackSimulationMaterials()
{
    static const std::vector<VoxelMaterialInfo> materials = {
        {0, "SiO2", 255, 105, 0},
        {1, "Si3N4", 0, 255, 0},
        {2, "Si", 128, 0, 128},
        {3, "DX", 0, 255, 255}
    };
    return materials;
}

Simulation stackSimulation() {
    Simulation simulation(
        Settings::X,
        Settings::Y,
        Settings::Z,
        Settings::voxelSize
    );

    Voxel SiO2;

    SiO2.solid = 1;
    SiO2.threshold = 1000;
    SiO2.depositThreshold = 70;
    SiO2.type = 0;

    Voxel Si3N4;

    Si3N4.solid = 1;
    Si3N4.threshold = 40;
    Si3N4.depositThreshold = 70;
    Si3N4.type = 1;

    Voxel Si;

    Si.solid = 1;
    Si.threshold = 50;
    Si.depositThreshold = 70;
    Si.type = 2;

    Voxel DX;

    DX.solid = 1;
    DX.threshold = 60;
    DX.depositThreshold = 100;
    DX.type = 3;


    // Bottom oxide
    simulation.initRectangle(
        SiO2,
        0, 0, 0,
        Settings::X, 20, Settings::X
    );

    // Alternating layers
    for (int i = 1; i <= 10; i++)
    {
        if (i % 2 == 0)
        {
            simulation.initRectangle(
                Si3N4,
                0, i * 20, 0,
                Settings::X, (i + 1) * 20, Settings::X
            );
        }
        else
        {
            simulation.initRectangle(
                Si,
                0, i * 20, 0,
                Settings::X, (i + 1) * 20, Settings::X
            );
        }
    }

    // Top oxide
    simulation.initRectangle(
        SiO2,
        0, 220, 0,
        Settings::X, 280, Settings::X
    );


    // ----------------------------------------------------
    // Create a cylindrical hole (diameter 24, radius 12)
    // through the top oxide layer
    // ----------------------------------------------------

    const int radius = 12;

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
