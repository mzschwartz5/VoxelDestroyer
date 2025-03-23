#include "pbd.h"

const float timeStep = 0.01f;

const std::vector<Particle>& PBD::simulateStep()
{
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

    return particles;
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
    for (const auto& position : positions)
    {
        Particle particle;
        particle.oldPosition = particle.newPosition = position;
        particle.velocity = glm::vec3(0.0f);
        particle.mass = 1.0f;
        particles.push_back(particle);
    }
}