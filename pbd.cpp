#include "pbd.h"
#include <maya/MGlobal.h>

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
        particle.oldPosition = particle.position;
        particle.position += particle.velocity * timeStep;
    }

    solveVolumeConstraint();

    for (auto& particle : particles)
    {
        particle.velocity = (particle.position - particle.oldPosition) / timeStep;
    }
}

void PBD::solveGroundCollision()
{
    for (auto& particle : particles)
    {
        if (particle.position.y < 0.0f)
        {
            particle.position.y = particle.oldPosition.y - particle.velocity.y * timeStep;
        }
    }
}

void PBD::solveDistanceConstraint()
{
    // For now, hardcode assume there are only two particles
    Particle& p1 = particles[0];
    Particle& p2 = particles[1];
    float desiredDistance = 5.0f;
    float compliance = 0.3f;

    glm::vec3 delta = p2.position - p1.position;
    float deltalen = glm::length(delta);
    if (deltalen == 0.0f) return;

    float C = deltalen - desiredDistance;
    glm::vec3 C1 = delta / deltalen;
    glm::vec3 C2 = -C1;

    float w_tot = p1.w + p2.w;
    if (w_tot == 0.0f) return;

    float alpha = compliance / (timeStep * timeStep);
    float lambda = C / (w_tot + alpha);

    p1.position += lambda * p1.w * C1;
    p2.position += lambda * p2.w * C2;
}

void PBD::solveVolumeConstraint() {

    // For now, hardcode assume four particles making up one tet
    Particle& p1 = particles[0];
    Particle& p2 = particles[1];
    Particle& p3 = particles[2];
    Particle& p4 = particles[3];

    float desiredVolume = 35.0f;
    float compliance = 0.3f;

    float w_tot = p1.w + p2.w + p3.w + p4.w;
    if (w_tot == 0.0f) return;

    glm::vec3 delC1 = glm::cross(p4.position - p2.position, p3.position - p2.position);
    glm::vec3 delC2 = glm::cross(p3.position - p1.position, p4.position - p1.position);
    glm::vec3 delC3 = glm::cross(p4.position - p1.position, p2.position - p1.position);
    glm::vec3 delC4 = glm::cross(p2.position - p1.position, p3.position - p1.position);
    float delC1Len = glm::length(delC1);
    float delC2Len = glm::length(delC2);
    float delC3Len = glm::length(delC3);
    float delC4Len = glm::length(delC4);


    float restVolume = glm::abs(glm::dot(delC4, p4.position - p1.position) / 6.0f);
    float C = desiredVolume - restVolume;
    float alpha = compliance / (timeStep * timeStep);
    float lambda = -C / (p1.w * delC1Len * delC1Len + p2.w * delC2Len * delC2Len + p3.w * delC3Len * delC3Len + p4.w * delC4Len * delC4Len + alpha);

    // Update the positions
    p1.position += lambda * p1.w * delC1;
    p2.position += lambda * p2.w * delC2;
    p3.position += lambda * p3.w * delC3;
    p4.position += lambda * p4.w * delC4;
}

PBD::PBD(const std::vector<glm::vec3>& positions)
{
    timeStep = 0.01f / substeps;

    for (const auto& position : positions)
    {
        Particle particle;
        particle.oldPosition = particle.position = position;
        particle.velocity = glm::vec3(0.0f);
        particle.w = 1.0f;
        particles.push_back(particle);
    }
}