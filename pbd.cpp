#include "pbd.h"
#include <maya/MGlobal.h>
#include <float.h>
#include "utils.h"
#include "constants.h"

void PBD::initialize(const Voxels& voxels, float voxelSize, const MDagPath& meshDagPath) {
    this->meshDagPath = meshDagPath;
    timeStep = (1.0f / 60.0f) / static_cast<float>(substeps);
    constructFaceToFaceConstraints(voxels);
    createParticles(voxels);
    setRadiusAndVolumeFromLength(voxelSize);

    MStatus status;
	MFnMesh voxelMeshFn(meshDagPath);

	// Calculate a local rest position for each vertex in every voxel.
	bindVerticesCompute = std::make_unique<BindVerticesCompute>(
		static_cast<int>(voxels.size()) * 8, // 8 particles per voxel
		voxelMeshFn.getRawPoints(&status),
		voxelMeshFn.numVertices(&status),
		voxels.vertStartIdx, 
		voxels.numVerts
	);

	bindVerticesCompute->updateParticleBuffer(particles.positions);
	bindVerticesCompute->dispatch(voxels.size());

	MGlobal::displayInfo("Bind vertices compute shader dispatched.");

	transformVerticesCompute = std::make_unique<TransformVerticesCompute>(
		voxelMeshFn.numVertices(&status),
		bindVerticesCompute->getParticlesSRV(), 			
		bindVerticesCompute->getVertStartIdxSRV(), 
		bindVerticesCompute->getNumVerticesSRV(), 
		bindVerticesCompute->getLocalRestPositionsSRV()
	);
	transformVerticesNumWorkgroups = voxels.size();

	MGlobal::displayInfo("Transform vertices compute shader initialized.");

    vgsCompute = std::make_unique<VGSCompute>(
        particles.numParticles,
        particles.w.data(),
        bindVerticesCompute->getParticlesUAV()
    );

}

void PBD::constructFaceToFaceConstraints(const Voxels& voxels) {
    for (int i = 0; i < voxels.numOccupied; i++) {
        uint32_t x, y, z;
        Utils::fromMortonCode(voxels.mortonCodes[i], x, y, z);

        int rightVoxelMortonCode = static_cast<int>(Utils::toMortonCode(x + 1, y, z));
        int upVoxelMortonCode = static_cast<int>(Utils::toMortonCode(x, y + 1, z));
        int frontVoxelMortonCode = static_cast<int>(Utils::toMortonCode(x, y, z + 1));

        // Checks that the right voxel is in the grid and is occupied
        if (voxels.mortonCodesToSortedIdx.find(rightVoxelMortonCode) != voxels.mortonCodesToSortedIdx.end()) {
            int rightNeighborIndex = voxels.mortonCodesToSortedIdx.at(rightVoxelMortonCode);

            FaceConstraint newConstraint;
            newConstraint.voxelOneIdx = i;
            newConstraint.voxelTwoIdx = rightNeighborIndex;
            newConstraint.compressionLimit = -FLT_MAX;
            newConstraint.tensionLimit = FLT_MAX;
            addFaceConstraint(newConstraint, 0);
        }

        // Checks that the up voxel is in the grid and is occupied
        if (voxels.mortonCodesToSortedIdx.find(upVoxelMortonCode) != voxels.mortonCodesToSortedIdx.end()) {
            int upNeighborIndex = voxels.mortonCodesToSortedIdx.at(upVoxelMortonCode);

            FaceConstraint newConstraint;
            newConstraint.voxelOneIdx = i;
            newConstraint.voxelTwoIdx = upNeighborIndex;
            newConstraint.compressionLimit = -FLT_MAX;
            newConstraint.tensionLimit = FLT_MAX;
            addFaceConstraint(newConstraint, 1);
        }

        // Checks that the front voxel is in the grid and is occupied
        if (voxels.mortonCodesToSortedIdx.find(frontVoxelMortonCode) != voxels.mortonCodesToSortedIdx.end()) {
            int frontNeighborIndex = voxels.mortonCodesToSortedIdx.at(frontVoxelMortonCode);

            FaceConstraint newConstraint;
            newConstraint.voxelOneIdx = i;
            newConstraint.voxelTwoIdx = frontNeighborIndex;
            newConstraint.compressionLimit = -FLT_MAX;
            newConstraint.tensionLimit = FLT_MAX;
            addFaceConstraint(newConstraint, 2);
        }
    }
}

void PBD::createParticles(const Voxels& voxels) {
    for (int i = 0; i < voxels.numOccupied; i++) {
        for (const auto& position : voxels.corners[i].corners) {
            particles.positions.push_back(vec4(position, 0.0f));
            particles.oldPositions.push_back(vec4(position, 0.0f));
            particles.velocities.push_back(vec4(0.0f));
            particles.w.push_back(1.0f);
            particles.numParticles++;
        }
    }
}

void PBD::simulateStep()
{
    for (int i = 0; i < substeps; i++)
    {
        simulateSubstep();
    }
}

void PBD::updateMeshVertices() {
	MFnMesh meshFn(meshDagPath);
	MFloatPointArray vertexArray;

	// For rendering, we need to update each voxel with its new basis, which we'll use to transform all vertices owned by that voxel
	bindVerticesCompute->updateParticleBuffer(particles.positions); // (owns the particles buffer)
	transformVerticesCompute->dispatch(transformVerticesNumWorkgroups);
	transformVerticesCompute->copyTransformedVertsToCPU(vertexArray, meshFn.numVertices());

	meshFn.setPoints(vertexArray, MSpace::kWorld);
	meshFn.updateSurface();
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

    bindVerticesCompute->updateParticleBuffer(particles.positions);
    int numVgsWorkgroups = ((particles.numParticles >> 3) + VGS_THREADS + 1) / (VGS_THREADS); 
    vgsCompute->dispatch(numVgsWorkgroups);
    vgsCompute->copyTransformedPositionsToCPU(particles.positions, bindVerticesCompute->getParticlesBuffer(), particles.numParticles);

    for (int i = 0; i < faceConstraints.size(); i++) {
        for (auto& constraint : faceConstraints[i]) {
            solveFaceConstraint(constraint, i);
        }
    }

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

void PBD::solveFaceConstraint(FaceConstraint& faceConstraint, int axis) {
    //check if constraint has already been broken
    if (faceConstraint.voxelOneIdx == -1 || faceConstraint.voxelTwoIdx == -1) {
        return;
    }

    auto voxelOneStartIdx = faceConstraint.voxelOneIdx << 3;
    auto voxelTwoStartIdx = faceConstraint.voxelTwoIdx << 3;

    std::array<vec3*, 4> faceOne; //midpoint particle positions for relevant face of voxel 1
    std::array<vec3*, 4> faceTwo; //midpoint particle positions for relevant face of voxel 2
    std::array<float, 4> faceOneW; //particle weights for relevant face of voxel 1
    std::array<float, 4> faceTwoW; //particle weights for relevant face of voxel 2
    std::array<int, 4> faceOneIndices; //particle indices for relevant face of voxel 1
    std::array<int, 4> faceTwoIndices; //particle indices for relevant face of voxel 2

    //calculate centers
    vec3 v1Center = glm::vec3((particles.positions[voxelOneStartIdx + 0] +
        particles.positions[voxelOneStartIdx + 1] +
        particles.positions[voxelOneStartIdx + 2] +
        particles.positions[voxelOneStartIdx + 3] +
        particles.positions[voxelOneStartIdx + 4] +
        particles.positions[voxelOneStartIdx + 5] +
        particles.positions[voxelOneStartIdx + 6] +
        particles.positions[voxelOneStartIdx + 7])) * 0.125f;

    vec3 v2Center = glm::vec3((particles.positions[voxelTwoStartIdx + 0] +
        particles.positions[voxelTwoStartIdx + 1] +
        particles.positions[voxelTwoStartIdx + 2] +
        particles.positions[voxelTwoStartIdx + 3] +
        particles.positions[voxelTwoStartIdx + 4] +
        particles.positions[voxelTwoStartIdx + 5] +
        particles.positions[voxelTwoStartIdx + 6] +
        particles.positions[voxelTwoStartIdx + 7])) * 0.125f;

    //calculate midpoint positions for every vertex
    std::array<vec3, 8> v1MidPositions;
    std::array<vec3, 8> v2MidPositions;

    for (int i = 0; i < 8; i++) {
        v1MidPositions[i] = (glm::vec3(particles.positions[voxelOneStartIdx + i]) + v1Center) * 0.5f;
        v2MidPositions[i] = (glm::vec3(particles.positions[voxelTwoStartIdx + i]) + v2Center) * 0.5f;
    }

    //define face indices based on axis
    switch (axis) {
    case 0: //x-axis
        faceOneIndices = { 1, 3, 5, 7 };
        faceTwoIndices = { 0, 2, 4, 6 };
        break;
    case 1: //y-axis
        faceOneIndices = { 2, 3, 6, 7 };
        faceTwoIndices = { 0, 1, 4, 5 };
        break;
    case 2: //z-axis
        faceOneIndices = { 4, 5, 6, 7 };
        faceTwoIndices = { 0, 1, 2, 3 };
        break;
    }

    //set up references to midpoint positions and get inverse masses
    for (int i = 0; i < 4; i++) {
        faceOne[i] = &v1MidPositions[faceOneIndices[i]];
        faceTwo[i] = &v2MidPositions[faceTwoIndices[i]];
        faceOneW[i] = particles.w[voxelOneStartIdx + faceOneIndices[i]];
        faceTwoW[i] = particles.w[voxelTwoStartIdx + faceTwoIndices[i]];
    }

    //check if constraint should be broken
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
				particles.positions[voxelOneStartIdx + faceOneIndices[i]] += glm::vec4(delta, 0.f);
            }

            if (faceTwoW[i] != 0.0f) {
                vec3 delta = *faceTwo[i] - origFaceTwo[i];
				particles.positions[voxelTwoStartIdx + faceTwoIndices[i]] += glm::vec4(delta, 0.f);
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
            if (axis == 1) V = -V; //hack from GPU code

            if (V < 0.0f) {
				std::string flipStr = "Constraint broken due to flipping on axis " + std::to_string(axis);
                MGlobal::displayInfo(flipStr.c_str());
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
					particles.positions[voxelOneStartIdx + faceOneIndices[i]] += glm::vec4(delta, 0.f);
                }

                if (faceTwoW[i] != 0.0f) {
                    vec3 delta = *faceTwo[i] - origFaceTwo[i];
                    particles.positions[voxelTwoStartIdx + faceTwoIndices[i]] += glm::vec4(delta, 0.f);
                }
            }
        }
    }
}