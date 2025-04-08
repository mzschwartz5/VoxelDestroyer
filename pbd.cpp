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
        std::array<Particle*, 8> voxelParticles = {
            &particles[i], &particles[i + 1], &particles[i + 2], &particles[i + 3],
            &particles[i + 4], &particles[i + 5], &particles[i + 6], &particles[i + 7]
        };

        // Sort particles based on their positions to match the desired winding order
        std::sort(voxelParticles.begin(), voxelParticles.end(), [](Particle* a, Particle* b) {
            // First sort by Z (back face before front face)
            if (a->position.z < b->position.z) return true;
            if (a->position.z > b->position.z) return false;

            // If Z is equal, sort by Y (bottom before top)
            if (a->position.y < b->position.y) return true;
            if (a->position.y > b->position.y) return false;

            // If Z and Y are equal, sort by X (left before right)
            return a->position.x < b->position.x;
            });

        // Assign sorted particles to the voxel
        voxel.particles[0] = *voxelParticles[0]; // Back face bottom left
        voxel.particles[1] = *voxelParticles[1]; // Back face bottom right
        voxel.particles[2] = *voxelParticles[2]; // Back face top left
        voxel.particles[3] = *voxelParticles[3]; // Back face top right
        voxel.particles[4] = *voxelParticles[4]; // Front face bottom left
        voxel.particles[5] = *voxelParticles[5]; // Front face bottom right
        voxel.particles[6] = *voxelParticles[6]; // Front face top left
        voxel.particles[7] = *voxelParticles[7]; // Front face top right

        glm::vec3& p0 = voxel.particles[0].position;
        glm::vec3& p1 = voxel.particles[1].position;
        glm::vec3& p2 = voxel.particles[2].position;
        glm::vec3& p3 = voxel.particles[3].position;
        glm::vec3& p4 = voxel.particles[4].position;
        glm::vec3& p5 = voxel.particles[5].position;
        glm::vec3& p6 = voxel.particles[6].position;
        glm::vec3& p7 = voxel.particles[7].position;

        glm::vec3 v0 = ((p1 - p0) + (p3 - p2) + (p5 - p4) + (p7 - p6)) / 4.0f;
        glm::vec3 v1 = ((p2 - p0) + (p3 - p1) + (p6 - p4) + (p7 - p5)) / 4.0f;
        glm::vec3 v2 = ((p4 - p0) + (p5 - p1) + (p6 - p2) + (p7 - p3)) / 4.0f;

        float relaxation = 0.5f, beta = 1.0f, particle_radius = 0.1f;

        glm::vec3 u0 = v0 - relaxation * (project(v0, v1) + project(v0, v2));
        glm::vec3 u1 = v1 - relaxation * (project(v1, v2) + project(v1, v0));
        glm::vec3 u2 = v2 - relaxation * (project(v2, v0) + project(v2, v1));

        float length0 = glm::length(u0);
        u0 = (u0 / length0) * ((1.f - beta) * particle_radius + (beta * (length0 * 0.5f)));
        float length1 = glm::length(u1);
        u1 = (u1 / length1) * ((1.f - beta) * particle_radius + (beta * (length1 * 0.5f)));
        float length2 = glm::length(u2);
        u2 = (u2 / length2) * ((1.f - beta) * particle_radius + (beta * (length2 * 0.5f)));

        voxel.volume = glm::dot(glm::cross(u0, u1), u2);
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
    float init_volume = voxel.volume;

    for (unsigned int i = 0; i < iter_count; i++) {
        glm::vec3& p0 = voxel.particles[0].position;
        glm::vec3& p1 = voxel.particles[1].position;
        glm::vec3& p2 = voxel.particles[2].position;
        glm::vec3& p3 = voxel.particles[3].position;
        glm::vec3& p4 = voxel.particles[4].position;
        glm::vec3& p5 = voxel.particles[5].position;
        glm::vec3& p6 = voxel.particles[6].position;
        glm::vec3& p7 = voxel.particles[7].position;
		glm::vec3 centroid = p0 + p1 + p2 + p3 + p4 + p5 + p6 + p7;
		centroid /= 8.0f;

		glm::vec3 v0 = ((p1 - p0) + (p3 - p2) + (p5 - p4) + (p7 - p6)) / 4.0f;
        glm::vec3 v1 = ((p2 - p0) + (p3 - p1) + (p6 - p4) + (p7 - p5)) / 4.0f;
        glm::vec3 v2 = ((p4 - p0) + (p5 - p1) + (p6 - p2) + (p7 - p3)) / 4.0f;

        glm::vec3 u0 = v0 - relaxation * (project(v0, v1) + project(v0, v2));
        glm::vec3 u1 = v1 - relaxation * (project(v1, v2) + project(v1, v0));
        glm::vec3 u2 = v2 - relaxation * (project(v2, v0) + project(v2, v1));

		float length0 = glm::length(u0);
        u0 = (u0 / length0) * ((1.f - beta) * particle_radius + (beta * (length0  * 0.5f)));
		float length1 = glm::length(u1);
		u1 = (u1 / length1) * ((1.f - beta) * particle_radius + (beta * (length1 * 0.5f)));
		float length2 = glm::length(u2);
		u2 = (u2 / length2) * ((1.f - beta) * particle_radius + (beta * (length2 * 0.5f)));
		
        float volume = glm::dot(glm::cross(u0, u1), u2);
        float mult = 0.5f * glm::pow((init_volume / volume), 1.0f / 3.0f);

        u0 *= mult;
        u1 *= mult;
        u2 *= mult;
		
		if (voxel.particles[0].w != 0.0f) {
            p0 = centroid - u0 - u1 - u2;
		}

        if (voxel.particles[1].w != 0.0f) {
            p1 = centroid + u0 - u1 - u2;
        }

		if (voxel.particles[2].w != 0.0f) {
			p2 = centroid - u0 + u1 - u2;
		}

        if (voxel.particles[3].w != 0.0f) {
            p3 = centroid + u0 + u1 - u2;
        }

		if (voxel.particles[4].w != 0.0f) {
			p4 = centroid - u0 - u1 + u2;
		}
        if (voxel.particles[5].w != 0.0f) {
            p5 = centroid + u0 - u1 + u2;
        }

		if (voxel.particles[6].w != 0.0f) {
			p6 = centroid - u0 + u1 + u2;
		}

        if (voxel.particles[7].w != 0.0f) {
            p7 = centroid + u0 + u1 + u2;
        }
    }
}