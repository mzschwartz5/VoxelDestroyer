#pragma once
#include "glm/glm.hpp"
#include <algorithm>
#include <vector>
#include <array>

struct Particle
{
    glm::vec3 position;
    glm::vec3 oldPosition;
    glm::vec3 velocity;
    float w = 0.0f; // inverse mass
};

struct Voxel {
	std::array<Particle, 8> particles;
    float volume;
};

class PBD
{
public:
    PBD() = default;
    PBD(const std::vector<glm::vec3>& positions);
    ~PBD() = default;
    const std::vector<Particle>& simulateStep();
    void simulateSubstep();

	std::vector<Particle> getParticles() const { return particles; }

private:
    std::vector<Particle> particles;
	std::vector<Voxel> voxels;
    int substeps = 10;
    float timeStep;

    // Constraint solvers
    void solveGroundCollision();

    void solveVGS(Voxel& voxel, float particle_radius, float relaxation, float beta, unsigned int iter_count);

    glm::vec3 project(glm::vec3 x, glm::vec3 y, glm::vec3 z, float relaxation);
};
