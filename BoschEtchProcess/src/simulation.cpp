#include "simulation.h"
#include "settings.h"
#include <cmath>
#include <random>
#include <iostream>
namespace Math {
    float randomFloat(float max)
    {
        static std::mt19937 gen{ std::random_device{}() };
        std::uniform_real_distribution<float> dist(0.0f, max);
        return dist(gen);
    }
}
Simulation::Simulation(int X_, int Y_, int Z_, float voxelSize_) : grid(X_, Y_, Z_) {
	X = X_;
	Y = Y_;
	Z = Z_;
    voxelSize = voxelSize_;
    particles.reserve(10000);
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

void Simulation::tick(float dt)
{
    for (Particle& p : particles)
    {
        if (!p.alive) continue;
        marchRay(p, p.speed * dt);
    }

    particles.erase(
        std::remove_if(particles.begin(), particles.end(),
            [](const Particle& p) { return !p.alive; }),
        particles.end()
    );
}
void Simulation::marchRay(Particle& p, float maxDist)
{
    int cx = int(std::floor(p.x / voxelSize));
    int cy = int(std::floor(p.y / voxelSize));
    int cz = int(std::floor(p.z / voxelSize));

    int lastCX = cx;
    int lastCY = cy;
    int lastCZ = cz;

    int stepX = (p.dx >= 0) ? 1 : -1;
    int stepY = (p.dy >= 0) ? 1 : -1;
    int stepZ = (p.dz >= 0) ? 1 : -1;

    float nextX = ((cx + (stepX > 0)) * voxelSize - p.x) / p.dx;
    float nextY = ((cy + (stepY > 0)) * voxelSize - p.y) / p.dy;
    float nextZ = ((cz + (stepZ > 0)) * voxelSize - p.z) / p.dz;

    float deltaX = voxelSize / std::abs(p.dx);
    float deltaY = voxelSize / std::abs(p.dy);
    float deltaZ = voxelSize / std::abs(p.dz);

    float traveled = 0.0f;

    constexpr float roughness = Settings::Roughness;
    constexpr float minEnergy = 1e-6f;

    while (traveled < maxDist && p.alive)
    {
        if (!grid.inBounds(cx, cy, cz)) {
            p.alive = false;
            break;
        }

        Voxel& v = grid.at(cx, cy, cz);

        if (v.solid)
        {
            // ===== damage & deposition =====

            float damage = std::max(p.energy * 0.3f, 0.01f);

            if (!p.deposit)
                v.threshold -= damage;
            else
                v.depositThreshold -= damage;

            p.energy -= damage;

            if (v.threshold <= 0.0f) {
                v.solid = false;
                v.type = 0;
            }

            // ===== deposition growth (unchanged) =====

            if (v.depositThreshold <= 0.0f)
            {
                int nx = cx + (cx - lastCX);
                int ny = cy + (cy - lastCY);
                int nz = cz + (cz - lastCZ);

                if (grid.inBounds(nx, ny, nz))
                {
                    Voxel& newV = grid.at(nx, ny, nz);

                    if (!newV.solid)
                    {
                        newV.solid = true;
                        newV.type = 2;
                        newV.threshold = 1000;
                        newV.depositThreshold = 150000;
                    }
                }

                v.depositThreshold = 5.0f + Math::randomFloat(15.0f);
            }

            // ===== reflect OR die =====

            float reflectProb = 0.6f;   // tune this

            if (Math::randomFloat(1) < reflectProb)
            {
                // reflect on entered face
                if (cx != lastCX) p.dx *= -1.0f;
                if (cy != lastCY) p.dy *= -1.0f;
                if (cz != lastCZ) p.dz *= -1.0f;

                // roughness
                p.dx += (Math::randomFloat(1) * 2 - 1) * roughness;
                p.dy += (Math::randomFloat(1) * 2 - 1) * roughness;
                p.dz += (Math::randomFloat(1) * 2 - 1) * roughness;

                float len = std::sqrt(p.dx * p.dx + p.dy * p.dy + p.dz * p.dz);
                p.dx /= len;
                p.dy /= len;
                p.dz /= len;

                stepX = (p.dx >= 0) ? 1 : -1;
                stepY = (p.dy >= 0) ? 1 : -1;
                stepZ = (p.dz >= 0) ? 1 : -1;

                deltaX = voxelSize / std::abs(p.dx);
                deltaY = voxelSize / std::abs(p.dy);
                deltaZ = voxelSize / std::abs(p.dz);

                nextX = ((cx + (stepX > 0)) * voxelSize - p.x) / p.dx;
                nextY = ((cy + (stepY > 0)) * voxelSize - p.y) / p.dy;
                nextZ = ((cz + (stepZ > 0)) * voxelSize - p.z) / p.dz;
            }
            else
            {
                // particle absorbed
                p.alive = false;
                break;
            }

            if (p.energy < minEnergy) {
                p.alive = false;
                break;
            }
        }

        // ===== advance ray =====

        lastCX = cx;
        lastCY = cy;
        lastCZ = cz;

        if (nextX < nextY && nextX < nextZ)
        {
            cx += stepX;
            traveled = nextX;
            nextX += deltaX;
        }
        else if (nextY < nextZ)
        {
            cy += stepY;
            traveled = nextY;
            nextY += deltaY;
        }
        else
        {
            cz += stepZ;
            traveled = nextZ;
            nextZ += deltaZ;
        }
    }

    p.x += p.dx * traveled;
    p.y += p.dy * traveled;
    p.z += p.dz * traveled;
}
