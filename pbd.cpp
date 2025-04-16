#include "pbd.h"
#include <maya/MGlobal.h>
#include <float.h>

PBD::PBD(const std::vector<vec3>& positions, float voxelSize) {
    timeStep = (1.0f / 60.0f) / static_cast<float>(substeps);

    for (const auto& position : positions) {
        particles.positions.push_back(vec4(position, 1.0f));
        particles.oldPositions.push_back(vec4(position, 1.0f));
        particles.velocities.push_back(vec4(0.0f));
        particles.w.push_back(1.0f);
        particles.numParticles++;
    }

    setRadiusAndVolumeFromLength(voxelSize);
}

const Particles& PBD::simulateStep()
{
    for (int i = 0; i < substeps; i++)
    {
        simulateSubstep();
    }

    return particles;
}

void PBD::simulateSubstep() {
    // Iterate over all particles and apply gravity
    for (int i = 0; i < particles.numParticles; ++i)
    {
        if (particles.w[i] == 0.0f) continue;
        particles.velocities[i] += vec4(0.0f, -10.0f, 0.0f, 0.0f) * timeStep;
        particles.oldPositions[i] = particles.positions[i];
        particles.positions[i] += particles.velocities[i] * timeStep;
    }

    solveGroundCollision();
    for (int i = 0; i < particles.numParticles; i += 8) {
		solveVGS(i, 3);
	}
    /*for (int i = 0; i < faceConstraints.size(); i++) {
        for (auto& constraint : faceConstraints[i]) {
            solveFaceConstraint(constraint, i);
        }
    }*/

    for (int i = 0; i < particles.numParticles; ++i)
    {
        if (particles.w[i] == 0.0f) continue;
        particles.velocities[i] = (particles.positions[i] - particles.oldPositions[i]) / timeStep;
    }
}

void PBD::solveGroundCollision()
{
    for (int i = 0; i < particles.numParticles; ++i)
    {
        if (particles.w[i] == 0.0f) continue;

        if (particles.positions[i].y < 0.0f)
        {
            particles.positions[i] = particles.oldPositions[i];
            particles.positions[i].y = 0.0f;
        }
    }
}

vec4 PBD::project(vec4 x, vec4 y) {
    return (glm::dot(y, x) / glm::dot(y, y)) * y;
}

void PBD::solveVGS(int start_idx, unsigned int iter_count) {
    for (unsigned int i = 0; i < iter_count; i++) {
        vec4& p0 = particles.positions[start_idx];
        vec4& p1 = particles.positions[start_idx + 1];
		vec4& p2 = particles.positions[start_idx + 2];
		vec4& p3 = particles.positions[start_idx + 3];
		vec4& p4 = particles.positions[start_idx + 4];
		vec4& p5 = particles.positions[start_idx + 5];
		vec4& p6 = particles.positions[start_idx + 6];
		vec4& p7 = particles.positions[start_idx + 7];

		vec4 centroid = p0 + p1 + p2 + p3 + p4 + p5 + p6 + p7;
		centroid *= 0.125f;

		vec4 v0 = ((p1 - p0) + (p3 - p2) + (p5 - p4) + (p7 - p6)) * 0.25f;
        vec4 v1 = ((p2 - p0) + (p3 - p1) + (p6 - p4) + (p7 - p5)) * 0.25f;
        vec4 v2 = ((p4 - p0) + (p5 - p1) + (p6 - p2) + (p7 - p3)) * 0.25f;
        
        vec4 u0 = v0 - RELAXATION * (project(v0, v1) + project(v0, v2));
        vec4 u1 = v1 - RELAXATION * (project(v1, v2) + project(v1, v0));
        vec4 u2 = v2 - RELAXATION * (project(v2, v0) + project(v2, v1));

        u0 = glm::normalize(u0) * ((1.f - BETA) * PARTICLE_RADIUS + (BETA * glm::length(v0)  * 0.5f));
		u1 = glm::normalize(u1) * ((1.f - BETA) * PARTICLE_RADIUS + (BETA * glm::length(v1) * 0.5f));
		u2 = glm::normalize(u2) * ((1.f - BETA) * PARTICLE_RADIUS + (BETA * glm::length(v2) * 0.5f));
		
        float volume = glm::dot(glm::cross(vec3(u0), vec3(u1)), vec3(u2));
        float mult = 0.5f * glm::pow((VOXEL_REST_VOLUME / volume), 1.0f / 3.0f);

        u0 *= mult;
        u1 *= mult;
        u2 *= mult;

		if (particles.w[start_idx] != 0.0f) {
            p0 = centroid - u0 - u1 - u2;
		}

        if (particles.w[start_idx + 1] != 0.0f) {
            p1 = centroid + u0 - u1 - u2;
        }

		if (particles.w[start_idx + 2] != 0.0f) {
			p2 = centroid - u0 + u1 - u2;
		}

        if (particles.w[start_idx + 3] != 0.0f) {
            p3 = centroid + u0 + u1 - u2;
        }

		if (particles.w[start_idx + 4] != 0.0f) {
			p4 = centroid - u0 - u1 + u2;
		}
        if (particles.w[start_idx + 5] != 0.0f) {
            p5 = centroid + u0 - u1 + u2;
        }

		if (particles.w[start_idx + 6] != 0.0f) {
			p6 = centroid - u0 + u1 + u2;
		}

        if (particles.w[start_idx + 7] != 0.0f) {
            p7 = centroid + u0 + u1 + u2;
        }
    }
}

/*void PBD::solveFaceConstraint(FaceConstraint& faceConstraint, int axis) {
    //check if constraint has already been broken
    if (faceConstraint.voxelOneIdx == -1 || faceConstraint.voxelTwoIdx == -1) {
        return;
    }

    Voxel& voxelOne = voxels[faceConstraint.voxelOneIdx];
    Voxel& voxelTwo = voxels[faceConstraint.voxelTwoIdx];

    std::array<vec3*, 4> faceOne; //midpoint particle positions for relevant face of voxel 1
    std::array<vec3*, 4> faceTwo; //midpoint particle positions for relevant face of voxel 2
    std::array<float, 4> faceOneW; //particle weights for relevant face of voxel 1
    std::array<float, 4> faceTwoW; //particle weights for relevant face of voxel 2
    std::array<int, 4> faceOneIndices; //particle indices for relevant face of voxel 1
    std::array<int, 4> faceTwoIndices; //particle indices for relevant face of voxel 2

    //calculate centers
    vec3 v1Center = (particles[voxelOne.particles[0]].position +
        particles[voxelOne.particles[1]].position +
        particles[voxelOne.particles[2]].position +
        particles[voxelOne.particles[3]].position +
        particles[voxelOne.particles[4]].position +
        particles[voxelOne.particles[5]].position +
        particles[voxelOne.particles[6]].position +
        particles[voxelOne.particles[7]].position) / 8.0f;

    vec3 v2Center = (particles[voxelTwo.particles[0]].position +
        particles[voxelTwo.particles[1]].position +
        particles[voxelTwo.particles[2]].position +
        particles[voxelTwo.particles[3]].position +
        particles[voxelTwo.particles[4]].position +
        particles[voxelTwo.particles[5]].position +
        particles[voxelTwo.particles[6]].position +
        particles[voxelTwo.particles[7]].position) / 8.0f;

    //calculate midpoint positions for every vertex
    std::array<vec3, 8> v1MidPositions;
    std::array<vec3, 8> v2MidPositions;

    for (int i = 0; i < 8; i++) {
        v1MidPositions[i] = (particles[voxelOne.particles[i]].position + v1Center) * 0.5f;
        v2MidPositions[i] = (particles[voxelTwo.particles[i]].position + v2Center) * 0.5f;
    }

    //define face indices based on axis, matching the GPU implementation
    switch (axis) {
    case 0: //x-axis
        //negative X face (voxel 1) to positive X face (voxel 2)
        faceOneIndices = { 0, 2, 4, 6 }; //-X face indices
        faceTwoIndices = { 1, 3, 5, 7 }; //+X face indices
        break;
    case 1: //y-axis
        //negative Y face (voxel 1) to positive Y face (voxel 2)
        faceOneIndices = { 0, 1, 4, 5 }; //-Y face indices
        faceTwoIndices = { 2, 3, 6, 7 }; //+Y face indices
        break;
    case 2: //z-axis
        //negative Z face (voxel 1) to positive Z face (voxel 2)
        faceOneIndices = { 0, 1, 2, 3 }; //-Z face indices
        faceTwoIndices = { 4, 5, 6, 7 }; //+Z face indices
        break;
    }

    //set up references to midpoint positions and get inverse masses
    for (int i = 0; i < 4; i++) {
        faceOne[i] = &v1MidPositions[faceOneIndices[i]];
        faceTwo[i] = &v2MidPositions[faceTwoIndices[i]];

        faceOneW[i] = particles[voxelOne.particles[faceOneIndices[i]]].w;
        faceTwoW[i] = particles[voxelTwo.particles[faceTwoIndices[i]]].w;
    }

    //check if constraint should be deleted (fracture check)
    bool enableFracture = true;
    if (enableFracture) {
        for (int i = 0; i < 4; i++) {
            vec3 u = *faceTwo[i] - *faceOne[i];
            float L = glm::length(u);
            float strain = (L - 2.0f * PARTICLE_RADIUS) / (2.0f * PARTICLE_RADIUS);

            if (strain > faceConstraint.tensionLimit || strain < faceConstraint.compressionLimit) {
                MGlobal::displayInfo("Constraint broken due to tension or compression");
                std::string vStr{ "Strain: " + std::to_string(strain) };
                MGlobal::displayInfo(vStr.c_str());
                faceConstraint.voxelOneIdx = -1;
                faceConstraint.voxelTwoIdx = -1;
                return;
            }
        }
    }

    //calculate midpoint face center
    vec3 centerOfVoxels = vec3(0.0f);
    for (int i = 0; i < 4; i++) {
        centerOfVoxels += *faceOne[i] + *faceTwo[i];
    }
    centerOfVoxels *= 0.125f;

    //calculate edge vectors for shape preservation
    vec3 dp[3];

    //check if either voxel is static
    float sumW1 = faceOneW[0] + faceOneW[1] + faceOneW[2] + faceOneW[3];
    float sumW2 = faceTwoW[0] + faceTwoW[1] + faceTwoW[2] + faceTwoW[3];

    if (sumW1 == 0.0f || sumW2 == 0.0f) {
        MGlobal::displayInfo("Static voxel");
        //handle static voxel case
        vec3 u1, u2, u0;

        if (sumW1 == 0.0f) { //voxel 1 is static
            u1 = (*faceOne[1] - *faceOne[0]) + (*faceOne[3] - *faceOne[2]);
            u2 = (*faceOne[2] - *faceOne[0]) + (*faceOne[3] - *faceOne[1]);
        }
        else { //voxel 2 is static
            u1 = (*faceTwo[1] - *faceTwo[0]) + (*faceTwo[3] - *faceTwo[2]);
            u2 = (*faceTwo[2] - *faceTwo[0]) + (*faceTwo[3] - *faceTwo[1]);
        }

        //calculate the normal vector based on axis
        if (axis == 0 || axis == 2) {
            u0 = glm::cross(u1, u2);
        }
        else {
            u0 = glm::cross(u2, u1);
        }

        //normalize and scale by radius
        dp[0] = glm::normalize(u1) * PARTICLE_RADIUS;
        dp[1] = glm::normalize(u2) * PARTICLE_RADIUS;
        dp[2] = glm::normalize(u0) * PARTICLE_RADIUS;

        //check if volume is negative
        float V = glm::dot(glm::cross(dp[0], dp[1]), dp[2]);
        if (V < 0.0f) {
            //GPU code has a comment about breaking the constraint
        }

        //save  original midpoint positions
        std::array<vec3, 4> origFaceOne, origFaceTwo;
        for (int i = 0; i < 4; i++) {
            origFaceOne[i] = *faceOne[i];
            origFaceTwo[i] = *faceTwo[i];
        }

        //update midpoint positions
        if (faceOneW[0] != 0.0f) *faceOne[0] = centerOfVoxels - dp[0] - dp[1] - dp[2];
        if (faceOneW[1] != 0.0f) *faceOne[1] = centerOfVoxels + dp[0] - dp[1] - dp[2];
        if (faceOneW[2] != 0.0f) *faceOne[2] = centerOfVoxels - dp[0] + dp[1] - dp[2];
        if (faceOneW[3] != 0.0f) *faceOne[3] = centerOfVoxels + dp[0] + dp[1] - dp[2];
        if (faceTwoW[0] != 0.0f) *faceTwo[0] = centerOfVoxels - dp[0] - dp[1] + dp[2];
        if (faceTwoW[1] != 0.0f) *faceTwo[1] = centerOfVoxels + dp[0] - dp[1] + dp[2];
        if (faceTwoW[2] != 0.0f) *faceTwo[2] = centerOfVoxels - dp[0] + dp[1] + dp[2];
        if (faceTwoW[3] != 0.0f) *faceTwo[3] = centerOfVoxels + dp[0] + dp[1] + dp[2];

        //apply delta from midpoint positions back to particle positions
        for (int i = 0; i < 4; i++) {
            if (faceOneW[i] != 0.0f) {
                vec3 delta = *faceOne[i] - origFaceOne[i];
                particles[voxelOne.particles[faceOneIndices[i]]].position += delta;
            }

            if (faceTwoW[i] != 0.0f) {
                vec3 delta = *faceTwo[i] - origFaceTwo[i];
                particles[voxelTwo.particles[faceTwoIndices[i]]].position += delta;
            }
        }
    } else {
        //no static voxels - apply iterative shape preservation
        float alpha = 0.9f;
        float alphaLen = 0.9f;

        for (int iter = 0; iter < 3; iter++) {
            //calculate edge vectors
            dp[0] = (*faceTwo[0] - *faceOne[0]) + (*faceTwo[1] - *faceOne[1]) +
                (*faceTwo[2] - *faceOne[2]) + (*faceTwo[3] - *faceOne[3]);

            dp[1] = (*faceOne[1] - *faceOne[0]) + (*faceOne[3] - *faceOne[2]) +
                (*faceTwo[1] - *faceTwo[0]) + (*faceTwo[3] - *faceTwo[2]);

            dp[2] = (*faceOne[2] - *faceOne[0]) + (*faceOne[3] - *faceOne[1]) +
                (*faceTwo[2] - *faceTwo[0]) + (*faceTwo[3] - *faceTwo[1]);

            //recalculate center
            centerOfVoxels = vec3(0.0f);
            for (int i = 0; i < 4; i++) {
                centerOfVoxels += *faceOne[i] + *faceTwo[i];
            }
            centerOfVoxels *= 0.125f;

            //apply orthogonalization
            auto proj = [](const vec3& u, const vec3& v) -> vec3 {
                const float eps = 1e-12f;
                return glm::dot(v, u) / (glm::dot(u, u) + eps) * u;
                };

            vec3 u0 = dp[0] - alpha * (proj(dp[1], dp[0]) + proj(dp[2], dp[0]));
            vec3 u1 = dp[1] - alpha * (proj(dp[0], dp[1]) + proj(dp[2], dp[1]));
            vec3 u2 = dp[2] - alpha * (proj(dp[0], dp[2]) + proj(dp[1], dp[2]));

            //check for flipping
            float V = glm::dot(glm::cross(u0, u1), u2);
            if (axis == 0) V = -V; //hack from GPU code

            if (V < 0.0f) {
                MGlobal::displayInfo("Constraint broken due to flipping");
                std::string vStr{ "Volume: " + std::to_string(V) };
                MGlobal::displayInfo(vStr.c_str());
                faceConstraint.voxelOneIdx = -1;
                faceConstraint.voxelTwoIdx = -1;
                return;
            }

            //calculate normalized and scaled edge vectors
            vec3 lenu = vec3(glm::length(u0), glm::length(u1), glm::length(u2)) + vec3(1e-12f);
            vec3 lenp = vec3(glm::length(dp[0]), glm::length(dp[1]), glm::length(dp[2])) + vec3(1e-12f);

            float r_v = pow(PARTICLE_RADIUS * PARTICLE_RADIUS * PARTICLE_RADIUS / (lenp[0] * lenp[1] * lenp[2]), 0.3333f);

            //scale change in position based on alpha
            dp[0] = u0 / lenu[0] * glm::mix(PARTICLE_RADIUS, lenp[0] * r_v, alphaLen);
            dp[1] = u1 / lenu[1] * glm::mix(PARTICLE_RADIUS, lenp[1] * r_v, alphaLen);
            dp[2] = u2 / lenu[2] * glm::mix(PARTICLE_RADIUS, lenp[2] * r_v, alphaLen);

            //save original midpoint positions
            std::array<vec3, 4> origFaceOne, origFaceTwo;
            for (int i = 0; i < 4; i++) {
                origFaceOne[i] = *faceOne[i];
                origFaceTwo[i] = *faceTwo[i];
            }

            //update midpoint positions
            if (faceOneW[0] != 0.0f) *faceOne[0] = centerOfVoxels - dp[0] - dp[1] - dp[2];
            if (faceOneW[1] != 0.0f) *faceOne[1] = centerOfVoxels + dp[0] - dp[1] - dp[2];
            if (faceOneW[2] != 0.0f) *faceOne[2] = centerOfVoxels - dp[0] + dp[1] - dp[2];
            if (faceOneW[3] != 0.0f) *faceOne[3] = centerOfVoxels + dp[0] + dp[1] - dp[2];
            if (faceTwoW[0] != 0.0f) *faceTwo[0] = centerOfVoxels - dp[0] - dp[1] + dp[2];
            if (faceTwoW[1] != 0.0f) *faceTwo[1] = centerOfVoxels + dp[0] - dp[1] + dp[2];
            if (faceTwoW[2] != 0.0f) *faceTwo[2] = centerOfVoxels - dp[0] + dp[1] + dp[2];
            if (faceTwoW[3] != 0.0f) *faceTwo[3] = centerOfVoxels + dp[0] + dp[1] + dp[2];

            //apply delta from midpoint positions back to particle positions
            for (int i = 0; i < 4; i++) {
                if (faceOneW[i] != 0.0f) {
                    vec3 delta = *faceOne[i] - origFaceOne[i];
                    particles[voxelOne.particles[faceOneIndices[i]]].position += delta;
                }

                if (faceTwoW[i] != 0.0f) {
                    vec3 delta = *faceTwo[i] - origFaceTwo[i];
                    particles[voxelTwo.particles[faceTwoIndices[i]]].position += delta;
                }
            }
        }
    }
}*/