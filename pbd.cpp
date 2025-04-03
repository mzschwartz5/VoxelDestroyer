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
        if (particle.w == 0.0f) continue;
        particle.velocity += glm::vec3(0.0f, -10.0f, 0.0f) * timeStep;
        particle.oldPosition = particle.position;
        particle.position += particle.velocity * timeStep;
    }

    solveGroundCollision();
    solveDistanceConstraint();
    solveVolumeConstraint();

    for (auto& particle : particles)
    {
        if (particle.w == 0.0f) continue;
        particle.velocity = (particle.position - particle.oldPosition) / timeStep;
    }
}

float PBD::getTetVolume(const Tetrahedron& tet) const
{
    const Particle& p1 = particles[tet.indices[0]];
    const Particle& p2 = particles[tet.indices[1]];
    const Particle& p3 = particles[tet.indices[2]];
    const Particle& p4 = particles[tet.indices[3]];

    return glm::dot(p4.position - p1.position, glm::cross(p2.position - p1.position, p3.position - p1.position)) / 6.0f;
}

float PBD::getEdgeLength(const Edge& edge) const
{
    const Particle& p1 = particles[edge.indices[0]];
    const Particle& p2 = particles[edge.indices[1]];

    return glm::length(p2.position - p1.position);
}

void PBD::solveGroundCollision()
{
    for (auto& particle : particles)
    {
        if (particle.w == 0.0f) continue;

        if (particle.position.y < 0.0f)
        {
            particle.position = particle.oldPosition;
            particle.position.y = 0.0f;
        }
    }
}

void PBD::solveDistanceConstraint()
{
    float compliance = 500.0f;
    float alpha = compliance / (timeStep * timeStep);

    for (const auto& edge: edges) {
        Particle& p1 = particles[edge.indices[0]];
        Particle& p2 = particles[edge.indices[1]];
        
        float w_tot = p1.w + p2.w;
        if (w_tot == 0.0f) continue;
    
        glm::vec3 delta = p1.position - p2.position;
        float deltalen = glm::length(delta);
        if (deltalen == 0.0f) continue;
    
        float C = deltalen - edge.restLength;
        glm::vec3 C1 = delta / deltalen;
        glm::vec3 C2 = -C1;
        float lambda = -C / (w_tot + alpha);

        p1.position += lambda * p1.w * C1;
        p2.position += lambda * p2.w * C2;
    }
}

void PBD::solveVolumeConstraint() 
{
    float compliance = 0.0f;
    float alpha = compliance / (timeStep * timeStep);
    
    for (const auto& tet: tetrahedra) {
        Particle& p1 = particles[tet.indices[0]];
        Particle& p2 = particles[tet.indices[1]];
        Particle& p3 = particles[tet.indices[2]];
        Particle& p4 = particles[tet.indices[3]];
    
        glm::vec3 delC1 = glm::cross(p4.position - p2.position, p3.position - p2.position) / 6.0f;
        glm::vec3 delC2 = glm::cross(p3.position - p1.position, p4.position - p1.position) / 6.0f;
        glm::vec3 delC3 = glm::cross(p4.position - p1.position, p2.position - p1.position) / 6.0f;
        glm::vec3 delC4 = glm::cross(p2.position - p1.position, p3.position - p1.position) / 6.0f;
        float delC1Len = glm::length(delC1);
        float delC2Len = glm::length(delC2);
        float delC3Len = glm::length(delC3);
        float delC4Len = glm::length(delC4);
        
        
        float w_tot = p1.w * delC1Len * delC1Len + p2.w * delC2Len * delC2Len + p3.w * delC3Len * delC3Len + p4.w * delC4Len * delC4Len;
        if (w_tot == 0.0f) continue;
        
        float C = getTetVolume(tet) - tet.restVolume;
        float lambda = -C / (w_tot + alpha);

        // Update the positions
        p1.position += lambda * p1.w * delC1;
        p2.position += lambda * p2.w * delC2;
        p3.position += lambda * p3.w * delC3;
        p4.position += lambda * p4.w * delC4;
    }
}

PBD::PBD(
    const std::vector<glm::vec3>& positions,
    const std::vector<std::array<int, 4>>& tetIndices,
    const std::vector<std::array<int, 2>>& edgeIndices
)
{
    timeStep = (1.0f / 60.0f) / static_cast<float>(substeps);

    for (const auto& position : positions)
    {
        Particle particle;
        particle.oldPosition = particle.position = position;
        particle.velocity = glm::vec3(0.0f);
        particles.push_back(particle);
    }

    for (const auto& tetrahedron : tetIndices)
    {
        Tetrahedron tet;
        tet.indices = tetrahedron;
        tet.restVolume = getTetVolume(tet);
        tetrahedra.push_back(tet);

        // Iterate over each particle in tet
        for (int i = 0; i < 4; i++)
        {
            Particle& p = particles[tet.indices[i]];
            p.w += (tet.restVolume > 0.0) ? (1.0f / (tet.restVolume / 4.0f)) : 0.0f;
        }
    }

    for (const auto& edge : edgeIndices)
    {
        Edge e;
        e.indices = edge;
        e.restLength = getEdgeLength(e);
        edges.push_back(e);
    }

}