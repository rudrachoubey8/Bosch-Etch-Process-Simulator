#pragma once
#include <iostream>
#include "structures.h"
#include "glad/glad.h"

class Simulation {

public:

	int X, Y, Z;
	float voxelSize;
	Grid grid;
	std::vector<Particle> particles;

	
	Simulation(int X, int Y, int Z, float voxelSize);

	
	void initRectangle(const Voxel& voxel, int x0, int y0, int z0, int x1, int y1, int z1);
	void initParticle(const Particle& particle);

	void tick(float dt);

	void createBuffers();
	void bindBuffers();
	void uploadParticles(std::vector<Particle>& particles);
	void uploadVoxels(std::vector<Voxel>& voxels);
	void dispatchRayMarch(GLuint program, int particleCount);


	std::vector<HitEvent> downloadHits();
	std::vector<Particle> downloadParticles();

	void resolveHitEvents(std::vector<HitEvent>& hitevents);

private:

	uint32_t MAX_PARTICLES = 1000000;
	uint32_t MAX_HITS = 50000;
	uint32_t MAX_STEPS = 5000;
	float MIN_ENERGY = 1e-6f;
	GLuint particleSSBO = 0, voxelSSBO = 0, hitSSBO = 0, counterSSBO = 0, rayMarchProgram = 0, finalParticlesCount = 0, finalParticles = 0;

};