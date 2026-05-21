
#include "stackSimulations.h"

Simulation stackSimulation() {

    Simulation simulation(
        Settings::X,
        Settings::Y,
        Settings::Z,
        Settings::voxelSize
    );

    Voxel SiO2;

    SiO2.solid = 1;
    SiO2.threshold = 100;
    SiO2.depositThreshold = 7;
    SiO2.type = 0;

    Voxel Si3N4;

    Si3N4.solid = 1;
    Si3N4.threshold = 4;
    Si3N4.depositThreshold = 7;
    Si3N4.type = 1;

    Voxel Si;

    Si.solid = 1;
    Si.threshold = 5;
    Si.depositThreshold = 7;
    Si.type = 2;

    Voxel DX;

    DX.solid = 1;
    DX.threshold = 6;
    DX.depositThreshold = 10;
    DX.type = 3;


    simulation.initRectangle(SiO2, 0, 0, 0, 25, 10, 25);
    for (int i = 1; i <= 10; i++)
    {
        if (i % 2 == 0) {
            simulation.initRectangle(Si3N4, 0, i * 10, 0, 25, (i + 1) * 10, 25);
        }
        else {
            simulation.initRectangle(Si, 0, i * 10, 0, 25, (i + 1) * 10, 25);
        }

    }
    simulation.initRectangle(SiO2, 0, 110, 0, 25, 140, 25);

    return simulation;
}