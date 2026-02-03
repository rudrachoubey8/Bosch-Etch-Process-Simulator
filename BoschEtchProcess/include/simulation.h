#pragma once
#include <iostream>
#include "structures.h"


class Simulation {

public:

	int X, Y, Z;
	float voxelSize;
	Grid grid;
	std::vector<Particle> particles;

	
	Simulation(int X, int Y, int Z, float voxelSize);

	
	void initRectangle(const Voxel& voxel, int x0, int y0, int z0, int x1, int y1, int z1);
	void initParticle(const Particle& particle);
	void marchRay(Particle& particle, float maxDistance);
	void tick(float dt);


};