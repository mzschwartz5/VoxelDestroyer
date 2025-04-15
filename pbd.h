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
	std::array<int, 8> particles; //indices into the global array of particles
    glm::vec3 center;
    float volume;
    float restVolume;
};

struct FaceConstraint {
    int voxelOneIdx;
    int voxelTwoIdx;
    float tensionLimit;
	float compressionLimit;
};

class PBD
{
public:
    PBD();
    ~PBD() = default;
    const std::vector<Particle>& simulateStep();
    void simulateSubstep();

	std::vector<Particle> getParticles() const { return particles; }

    Voxel& addParticlesAndMakeVoxel(std::array<glm::vec3, 8> particlesToAdd, int voxelCount);

    void setParticleRadius() { PARTICLE_RADIUS = glm::length(particles[voxels[0].particles[1]].position - particles[voxels[0].particles[2]].position) * 0.25f; }

private:
    std::vector<Particle> particles;
	std::vector<Voxel> voxels;
    std::array<std::vector<FaceConstraint>, 3> faceConstraints; //0 = x, 1 = y, 2 = z
    int substeps = 10;
    float timeStep;

    // Constraint solvers
    void solveGroundCollision();

    void solveVGS(Voxel& voxel, unsigned int iter_count);

    void solveFaceConstraint(FaceConstraint& faceConstraint, int axis);

    glm::vec3 project(glm::vec3 x, glm::vec3 y);

    float BETA{ 0.99f };
    float PARTICLE_RADIUS{ 0.1f };
    float RELAXATION{ 0.5f };
};
