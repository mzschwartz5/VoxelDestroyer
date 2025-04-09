#include "pbd.h"
#include <maya/MGlobal.h>

PBD::PBD(const std::vector<glm::vec3>& positions) {
    timeStep = (1.0f / 60.0f) / static_cast<float>(substeps);

    for (const auto& position : positions) {
        Particle particle;
        particle.oldPosition = particle.position = position;
        particle.velocity = glm::vec3(0.0f);
        particle.w = 1.0f;
        particles.push_back(particle);
    }

    for (int i = 0; i < particles.size(); i += 8) {
		Voxel voxel;
        std::array<int, 8> voxelParticles = {i, i + 1, i + 2, i + 3, i + 4, i + 5, i + 6, i + 7};

        // Sort particles based on their positions to match the desired winding order
        std::sort(voxelParticles.begin(), voxelParticles.end(), [this](int a, int b) {
            // First sort by Z (back face before front face)
            if (particles[a].position.z < particles[b].position.z) return true;
            if (particles[a].position.z > particles[b].position.z) return false;

            // If Z is equal, sort by Y (bottom before top)
            if (particles[a].position.y < particles[b].position.y) return true;
            if (particles[a].position.y > particles[b].position.y) return false;

            // If Z and Y are equal, sort by X (left before right)
            return particles[a].position.x < particles[b].position.x;
        });

        voxel.particles = voxelParticles;
        voxel.volume = 1.0f; // use the edge length cubed later
        voxel.restVolume = voxel.volume;
        voxels.push_back(voxel);
    }
}

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
	for (auto& voxel : voxels) {
		solveVGS(voxel, 0.1f, 0.5f, 1.0f, 3);
	}

    for (auto& particle : particles)
    {
        if (particle.w == 0.0f) continue;
        particle.velocity = (particle.position - particle.oldPosition) / timeStep;
    }
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

glm::vec3 PBD::project(glm::vec3 x, glm::vec3 y) {
    return (glm::dot(y, x) / glm::dot(y, y)) * y;
}

void PBD::solveVGS(Voxel& voxel, float particle_radius, float relaxation, float beta, unsigned int iter_count) {
    for (unsigned int i = 0; i < iter_count; i++) {
        glm::vec3& p0 = particles[voxel.particles[0]].position;
        glm::vec3& p1 = particles[voxel.particles[1]].position;
        glm::vec3& p2 = particles[voxel.particles[2]].position;
        glm::vec3& p3 = particles[voxel.particles[3]].position;
        glm::vec3& p4 = particles[voxel.particles[4]].position;
        glm::vec3& p5 = particles[voxel.particles[5]].position;
        glm::vec3& p6 = particles[voxel.particles[6]].position;
        glm::vec3& p7 = particles[voxel.particles[7]].position;
		glm::vec3 centroid = p0 + p1 + p2 + p3 + p4 + p5 + p6 + p7;
		centroid /= 8.0f;

		glm::vec3 v0 = ((p1 - p0) + (p3 - p2) + (p5 - p4) + (p7 - p6)) / 4.0f;
        glm::vec3 v1 = ((p2 - p0) + (p3 - p1) + (p6 - p4) + (p7 - p5)) / 4.0f;
        glm::vec3 v2 = ((p4 - p0) + (p5 - p1) + (p6 - p2) + (p7 - p3)) / 4.0f;
        
        glm::vec3 u0 = v0 - relaxation * (project(v0, v1) + project(v0, v2));
        glm::vec3 u1 = v1 - relaxation * (project(v1, v2) + project(v1, v0));
        glm::vec3 u2 = v2 - relaxation * (project(v2, v0) + project(v2, v1));

        u0 = glm::normalize(u0) * ((1.f - beta) * particle_radius + (beta * glm::length(v0)  * 0.5f));
		u1 = glm::normalize(u1) * ((1.f - beta) * particle_radius + (beta * glm::length(v1) * 0.5f));
		u2 = glm::normalize(u2) * ((1.f - beta) * particle_radius + (beta * glm::length(v2) * 0.5f));
		
        float volume = glm::dot(glm::cross(u0, u1), u2);
        float mult = 0.5f * glm::pow((voxel.restVolume / volume), 1.0f / 3.0f);

        u0 *= mult;
        u1 *= mult;
        u2 *= mult;

		if (particles[voxel.particles[0]].w != 0.0f) {
            p0 = centroid - u0 - u1 - u2;
		}

        if (particles[voxel.particles[1]].w != 0.0f) {
            p1 = centroid + u0 - u1 - u2;
        }

		if (particles[voxel.particles[2]].w != 0.0f) {
			p2 = centroid - u0 + u1 - u2;
		}

        if (particles[voxel.particles[3]].w != 0.0f) {
            p3 = centroid + u0 + u1 - u2;
        }

		if (particles[voxel.particles[4]].w != 0.0f) {
			p4 = centroid - u0 - u1 + u2;
		}
        if (particles[voxel.particles[5]].w != 0.0f) {
            p5 = centroid + u0 - u1 + u2;
        }

		if (particles[voxel.particles[6]].w != 0.0f) {
			p6 = centroid - u0 + u1 + u2;
		}

        if (particles[voxel.particles[7]].w != 0.0f) {
            p7 = centroid + u0 + u1 + u2;
        }
    }
}