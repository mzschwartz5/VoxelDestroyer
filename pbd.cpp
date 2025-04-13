#include "pbd.h"
#include <maya/MGlobal.h>
#include <float.h>

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

    PARTICLE_RADIUS = glm::length(particles[voxels[0].particles[1]].position - particles[voxels[0].particles[2]].position) * 0.25f;

    //create face to face constraints
    FaceConstraint faceConstraint;
    faceConstraint.voxelOneIdx = 0;
    faceConstraint.voxelTwoIdx = 1;
    faceConstraint.compressionLimit = PARTICLE_RADIUS * 0.1f;
    faceConstraint.tensionLimit = FLT_MAX;
    faceConstraints[0].push_back(faceConstraint);
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
    int idx = 0;
    for (auto& particle : particles)
    {
        if (particle.w == 0.0f) continue;
        particle.velocity += glm::vec3(0.0f, -10.0f, 0.0f) * timeStep;
        /*if (idx < 8) {
            particle.velocity += glm::vec3(0.01f, 0.f, 0.f);
        }
        else {
            particle.velocity += glm::vec3(-0.01f, 0.f, 0.f);
        }*/
        idx++;
        particle.oldPosition = particle.position;
        particle.position += particle.velocity * timeStep;
    }

    solveGroundCollision();
	for (auto& voxel : voxels) {
		solveVGS(voxel, 3);
	}
    for (int i = 0; i < faceConstraints.size(); i++) {
        for (auto& constraint : faceConstraints[i]) {
            solveFaceConstraint(constraint, i);
        }
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

void PBD::solveVGS(Voxel& voxel, unsigned int iter_count) {
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
        
        glm::vec3 u0 = v0 - RELAXATION * (project(v0, v1) + project(v0, v2));
        glm::vec3 u1 = v1 - RELAXATION * (project(v1, v2) + project(v1, v0));
        glm::vec3 u2 = v2 - RELAXATION * (project(v2, v0) + project(v2, v1));

        u0 = glm::normalize(u0) * ((1.f - BETA) * PARTICLE_RADIUS + (BETA * glm::length(v0)  * 0.5f));
		u1 = glm::normalize(u1) * ((1.f - BETA) * PARTICLE_RADIUS + (BETA * glm::length(v1) * 0.5f));
		u2 = glm::normalize(u2) * ((1.f - BETA) * PARTICLE_RADIUS + (BETA * glm::length(v2) * 0.5f));
		
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

void PBD::solveFaceConstraint(FaceConstraint& faceConstraint, int axis) {
    //constraint has already beem broken
	if (faceConstraint.voxelOneIdx == -1 || faceConstraint.voxelTwoIdx == -1) {
		return;
	}

    Voxel& voxelOne = voxels[faceConstraint.voxelOneIdx];
    Voxel& voxelTwo = voxels[faceConstraint.voxelTwoIdx];

	glm::vec3& v1p0 = particles[voxelOne.particles[0]].position;
	glm::vec3& v1p1 = particles[voxelOne.particles[1]].position;
	glm::vec3& v1p2 = particles[voxelOne.particles[2]].position;
	glm::vec3& v1p3 = particles[voxelOne.particles[3]].position;
	glm::vec3& v1p4 = particles[voxelOne.particles[4]].position;
	glm::vec3& v1p5 = particles[voxelOne.particles[5]].position;
	glm::vec3& v1p6 = particles[voxelOne.particles[6]].position;
	glm::vec3& v1p7 = particles[voxelOne.particles[7]].position;

	glm::vec3& v2p0 = particles[voxelTwo.particles[0]].position;
	glm::vec3& v2p1 = particles[voxelTwo.particles[1]].position;
	glm::vec3& v2p2 = particles[voxelTwo.particles[2]].position;
	glm::vec3& v2p3 = particles[voxelTwo.particles[3]].position;
	glm::vec3& v2p4 = particles[voxelTwo.particles[4]].position;
	glm::vec3& v2p5 = particles[voxelTwo.particles[5]].position;
	glm::vec3& v2p6 = particles[voxelTwo.particles[6]].position;
	glm::vec3& v2p7 = particles[voxelTwo.particles[7]].position;

	glm::vec3 cv1 = (v1p0 + v1p1 + v1p2 + v1p3 + v1p4 + v1p5 + v1p6 + v1p7) / 8.0f;
	glm::vec3 cv2 = (v2p0 + v2p1 + v2p2 + v2p3 + v2p4 + v2p5 + v2p6 + v2p7) / 8.0f;

	glm::vec3 cv1p0 = (v1p0 + cv1) / 2.0f;
	glm::vec3 cv1p1 = (v1p1 + cv1) / 2.0f;
	glm::vec3 cv1p2 = (v1p2 + cv1) / 2.0f;
	glm::vec3 cv1p3 = (v1p3 + cv1) / 2.0f;
	glm::vec3 cv1p4 = (v1p4 + cv1) / 2.0f;
	glm::vec3 cv1p5 = (v1p5 + cv1) / 2.0f;
	glm::vec3 cv1p6 = (v1p6 + cv1) / 2.0f;
	glm::vec3 cv1p7 = (v1p7 + cv1) / 2.0f;
	std::array<glm::vec3, 8> cv1p = { cv1p0, cv1p1, cv1p2, cv1p3, cv1p4, cv1p5, cv1p6, cv1p7 };

	glm::vec3 cv2p0 = (v2p0 + cv2) / 2.0f;
	glm::vec3 cv2p1 = (v2p1 + cv2) / 2.0f;
	glm::vec3 cv2p2 = (v2p2 + cv2) / 2.0f;
	glm::vec3 cv2p3 = (v2p3 + cv2) / 2.0f;
	glm::vec3 cv2p4 = (v2p4 + cv2) / 2.0f;
	glm::vec3 cv2p5 = (v2p5 + cv2) / 2.0f;
	glm::vec3 cv2p6 = (v2p6 + cv2) / 2.0f;
	glm::vec3 cv2p7 = (v2p7 + cv2) / 2.0f;
	std::array<glm::vec3, 8> cv2p = { cv2p0, cv2p1, cv2p2, cv2p3, cv2p4, cv2p5, cv2p6, cv2p7 };

    std::array<std::pair<std::pair<int, int>, float>, 4 > edges; //id, id, length

    //length of 4 edges crossing constraint - voxel 1 is guaranteed to be to the left, below, or in front of voxel 2
    float l1, l2, l3, l4;
    switch (axis) {
    case 0:
        //l1 = 1 to 0
		l1 = glm::length(cv1p1 - cv2p0);
        edges[0] = { {1, 0}, l1 };

        //l2 = 5 to 4
		l2 = glm::length(cv1p5 - cv2p4);
		edges[1] = { {5, 4}, l2 };

        //l3 = 3 to 2
		l3 = glm::length(cv1p3 - cv2p2);
		edges[2] = { {3, 2}, l3 };

		//l4 = 7 to 6
		l4 = glm::length(cv1p7 - cv2p6);
		edges[3] = { {7, 6}, l4 };

        break;
    case 1:
        //l1 = 6 to 4
        l1 = glm::length(cv1p6 - cv2p4);
        edges[0] = { {6, 4}, l1 };

        //l2 = 7 to 5
		l2 = glm::length(cv1p7 - cv2p5);
		edges[1] = { {7, 5}, l2 };

        //l3 = 2 to 0
        l3 = glm::length(cv1p2 - cv2p0);
		edges[2] = { {2, 0}, l3 };

		//l4 = 3 to 1
        l4 = glm::length(cv1p3 - cv2p1);
		edges[3] = { {3, 1}, l4 };

        break;
    case 2:
        //l1 = 0 to 4
        l1 = glm::length(cv1p0 - cv2p4);
		edges[0] = { {0, 4}, l1 };

		//l2 = 1 to 5
        l2 = glm::length(cv1p1 - cv2p5);
		edges[1] = { {1, 5}, l2 };

		//l3 = 2 to 6
		l3 = glm::length(cv1p2 - cv2p6);
		edges[2] = { {2, 6}, l3 };

		//l4 = 3 to 7
		l4 = glm::length(cv1p3 - cv2p7);
		edges[3] = { {3, 7}, l4 };

        break;
    }

    float radiusTimesTwo = 2 * PARTICLE_RADIUS;
	float minStrain = std::min(std::min((l1 - radiusTimesTwo)  / radiusTimesTwo, (l2 - radiusTimesTwo) / radiusTimesTwo), 
        std::min((l3 - radiusTimesTwo) / radiusTimesTwo, (l4 - radiusTimesTwo) / radiusTimesTwo));
    float maxStrain = std::max(std::max((l1 - radiusTimesTwo) / radiusTimesTwo, (l2 - radiusTimesTwo) / radiusTimesTwo),
        std::max((l3 - radiusTimesTwo) / radiusTimesTwo, (l4 - radiusTimesTwo) / radiusTimesTwo));

    //break the constraint
	if (maxStrain > faceConstraint.tensionLimit || minStrain < faceConstraint.compressionLimit) {
        faceConstraint.voxelOneIdx = -1;
        faceConstraint.voxelTwoIdx = -1;
        return;
	}

    //check if constraint is inside out(volume < 0) - if yes, invert shortest edge

    //enforce distance

    for (const auto& edge : edges) {
        auto& particlesInEdge = edge.first;
        Particle& p1 = particles[voxelOne.particles[particlesInEdge.first]];
        Particle& p2 = particles[voxelTwo.particles[particlesInEdge.second]];

        float w_tot = p1.w + p2.w;
        if (w_tot == 0.0f) continue;

		glm::vec3 delta = cv2p[voxelTwo.particles[particlesInEdge.second] % 8] - cv1p[voxelOne.particles[particlesInEdge.first] % 8]; //can i still do this with the "centered" particles?
        float deltalen = edge.second;
        if (deltalen == 0.0f) continue;

        float C = glm::clamp(deltalen - PARTICLE_RADIUS * 2.0f, -1.0f, 1.0f);
        glm::vec3 C1 = delta / deltalen;
        glm::vec3 C2 = -C1;
        float lambda = -C / w_tot;

        p1.position += lambda * p1.w * C1;
        p2.position += lambda * p2.w * C2;
    }
}