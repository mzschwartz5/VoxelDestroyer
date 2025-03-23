#pragma once
#include "glm/glm.hpp"
#include <vector>

struct Particle
{
    glm::vec3 newPosition;
    glm::vec3 oldPosition;
    glm::vec3 velocity;
    float w; // inverse mass
};

class PBD
{
public:
    PBD() = default;
    PBD(const std::vector<glm::vec3>& positions);
    ~PBD() = default;
    const std::vector<Particle>& simulateStep();
    void simulateSubstep();

private:
    std::vector<Particle> particles;
    int substeps = 1;
    float timeStep;

    // Constraint solvers
    void solveGroundCollision();
    void solveDistanceConstraint();
};