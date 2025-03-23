#include "pbd.h"

const std::vector<Particle>& PBD::simulateStep()
{
    // Iterate over all particles and apply gravity
    for (auto& particle : particles)
    {
        particle.velocity += glm::vec3(0.0f, -9.81f, 0.0f) * 0.01f;
        particle.position += particle.velocity * 0.01f;
    }

    return particles;
}

PBD::PBD(const std::vector<glm::vec3>& positions)
{
    for (const auto& position : positions)
    {
        Particle particle;
        particle.position = position;
        particle.velocity = glm::vec3(0.0f);
        particle.mass = 1.0f;
        particles.push_back(particle);
    }
}