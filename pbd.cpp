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
        particle.velocity += glm::vec3(0.0f, -9.81f, 0.0f) * timeStep;
        particle.oldPosition = particle.newPosition;
        particle.newPosition += particle.velocity * timeStep;
    }

    solveGroundCollision();

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

PBD::PBD(const std::vector<glm::vec3>& positions)
{
    timeStep = 0.01f / substeps;

    for (const auto& position : positions)
    {
        Particle particle;
        particle.oldPosition = particle.newPosition = position;
        particle.velocity = glm::vec3(0.0f);
        particle.mass = 1.0f;
        particles.push_back(particle);
    }
}