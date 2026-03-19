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
	GLuint voxelSSBO = 0;

	Simulation(int X, int Y, int Z, float voxelSize);


	void initRectangle(const Voxel& voxel, int x0, int y0, int z0, int x1, int y1, int z1);
	void initParticle(const Particle& particle);
	void setVoxel(int x, int y, int z, Voxel v);

	void tick(float dt);

	void createBuffers();
	void bindBuffers();
	void uploadParticles(int n, bool type, bool deposit, float random);
	void uploadVoxels(std::vector<Voxel>& voxels);
	void dispatchRayMarch(GLuint program, int particleCount);
	void dispatchHits(GLuint program);

	std::vector<HitEvent> downloadHits();
	void downloadVoxels();
	int getParticleCount();
	
private:

	uint32_t MAX_PARTICLES = 1000000;
	uint32_t MAX_HITS = 50000;
	uint32_t MAX_STEPS = 5000;
	float MIN_ENERGY = 1e-6f;
	GLuint particleSSBO = 0, hitSSBO = 0, counterSSBO = 0, rayMarchProgram = 0, finalParticlesCount = 0, finalParticles = 0;
	GLuint resolveHitsProgram = 0;
	GLuint initParticlesProgram = 0;
};