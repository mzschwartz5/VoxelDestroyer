#include "pbd.h"

const std::vector<Particle>& PBD::simulateStep()
{
    for (int i = 0; i < substeps; i++)
    {
        simulateSubstep();
    }

    return particles;
}

void PBD::simulateSubstep() {
    // Iterate over all particles and apply gravity
    for (auto& particle : particles)
    {
        // particle.velocity += glm::vec3(0.0f, -9.81f, 0.0f) * timeStep;
        particle.oldPosition = particle.newPosition;
        particle.newPosition += particle.velocity * timeStep;
    }

    solveDistanceConstraint();

    for (auto& particle : particles)
    {
        particle.velocity = (particle.newPosition - particle.oldPosition) / timeStep;
    }
}

void PBD::solveGroundCollision()
{
    for (auto& particle : particles)
    {
        if (particle.newPosition.y < 0.0f)
        {
            particle.newPosition.y = particle.oldPosition.y - particle.velocity.y * timeStep;
        }
    }
}

void PBD::solveDistanceConstraint()
{
    // For now, hardcode assume there are only two particles
    Particle& p1 = particles[0];
    Particle& p2 = particles[1];
    float desiredDistance = 5.0f;

    glm::vec3 delta = p2.newPosition - p1.newPosition;

    float C = glm::length(delta) - desiredDistance;
    glm::vec3 C1 = delta / glm::length(delta);

    float C1_norm = glm::length(C1);
    glm::vec3 C2 = -C1;
    float C2_norm = C1_norm;

    float lambda = C / (p1.w * C1_norm * C1_norm + p2.w * C2_norm * C2_norm);

    p1.newPosition += lambda * p1.w * C1;
    p2.newPosition += lambda * p2.w * C2;
}

PBD::PBD(const std::vector<glm::vec3>& positions)
{
    timeStep = 0.01f / substeps;

    for (const auto& position : positions)
    {
        Particle particle;
        particle.oldPosition = particle.newPosition = position;
        particle.velocity = glm::vec3(0.0f);
        particle.w = 1.0f;
        particles.push_back(particle);
    }
}