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
	std::vector<Chunk> chunks;

	GLuint chunkSSBO = 0;
	GLuint voxelSSBO = 0;

	int numChunkX, numChunkY, numChunkZ;

	Simulation(int X, int Y, int Z, float voxelSize);


	void initRectangle(const Voxel& voxel, int x0, int y0, int z0, int x1, int y1, int z1);
	void initParticle(const Particle& particle);
	void setVoxel(int x, int y, int z, Voxel v);

	void tick(std::vector<float> gridData, int typesOfVoxels, int typesOfParticles);

	void createBuffers();
	void bindBuffers();

	void chunk();

	void uploadParticles(ParticleTypeData p, int type);
	void uploadChunks(std::vector<Chunk>& chunks, std::vector<Voxel>& voxels);
	void dispatchRayMarch(GLuint program, int particleCount, std::vector<float> gridData, int typesOfVoxels, int typesOfParticles);
	void dispatchHits(GLuint program);

	std::vector<HitEvent> downloadHits();
	void downloadChunks();
	void reset();
	int getParticleCount();
	

private:


	uint32_t MAX_PARTICLES = 1000000;
	uint32_t MAX_HITS = 50000;
	uint32_t MAX_STEPS = 5000;
	float MIN_ENERGY = 1e-6f;
	GLuint particleSSBO = 0, hitSSBO = 0, reactionProbabilitiesSSBO = 0, counterSSBO = 0, axesSSBO = 0, rayMarchProgram = 0, finalParticlesCount = 0, finalParticles = 0;
	GLuint resolveHitsProgram = 0;
	GLuint initParticlesProgram = 0;
};