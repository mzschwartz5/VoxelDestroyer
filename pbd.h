#pragma once
#include "glm/glm.hpp"
#include <algorithm>
#include <vector>
#include <array>
using glm::vec3;
using glm::vec4;

struct Particles
{
    std::vector<vec4> positions;
    std::vector<vec4> oldPositions;
    std::vector<vec4> velocities;
    std::vector<float> w; // inverse mass
    int numParticles{ 0 };
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
    PBD(const std::vector<vec3>& positions, float voxelSize);
    ~PBD() = default;
    const Particles& simulateStep();
    void simulateSubstep();

	Particles getParticles() const { return particles; }

private:
    Particles particles;
    //std::array<std::vector<FaceConstraint>, 3> faceConstraints; //0 = x, 1 = y, 2 = z
    int substeps = 10;
    float timeStep;

    // Constraint solvers
    void solveGroundCollision();

    void solveVGS(int start_idx, unsigned int iter_count);

    //void solveFaceConstraint(FaceConstraint& faceConstraint, int axis);

    vec4 project(vec4 x, vec4 y);

    float BETA{ 0.99f };
    float PARTICLE_RADIUS{ 0.1f };
    float RELAXATION{ 0.5f };
    float VOXEL_REST_VOLUME{ 1.0f };

    void setRadiusAndVolumeFromLength(float edge_length) {
        PARTICLE_RADIUS = edge_length * 0.25f;
        VOXEL_REST_VOLUME = edge_length * edge_length * edge_length;
    }
};
