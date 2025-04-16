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

//struct FaceConstraint {
//    int voxelOneIdx;
//    int voxelTwoIdx;
//    float tensionLimit;
//	float compressionLimit;
//};

class PBD
{
public:
    PBD() = default;
    PBD(const std::vector<glm::vec3>& positions, float voxelSize);
    ~PBD() = default;
    const std::vector<Particle>& simulateStep();
    void simulateSubstep();

	std::vector<Particle> getParticles() const { return particles; }

private:
    std::vector<Particle> particles;
    //std::array<std::vector<FaceConstraint>, 3> faceConstraints; //0 = x, 1 = y, 2 = z
    int substeps = 10;
    float timeStep;

    // Constraint solvers
    void solveGroundCollision();

    void solveVGS(int start_idx, unsigned int iter_count);

    //void solveFaceConstraint(FaceConstraint& faceConstraint, int axis);

    glm::vec3 project(glm::vec3 x, glm::vec3 y);

    float BETA{ 0.99f };
    float PARTICLE_RADIUS{ 0.1f };
    float RELAXATION{ 0.5f };
    float VOXEL_REST_VOLUME{ 1.0f };

    void setRadiusAndVolumeFromLength(float edge_length) {
        PARTICLE_RADIUS = edge_length * 0.25f;
        VOXEL_REST_VOLUME = edge_length * edge_length * edge_length;
    }
};
