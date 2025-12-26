#include "vgs_core.hlsl"
#include "faceconstraints_shared.hlsl"

void breakConstraint(int constraintIdx, int voxelAIdx, int voxelBIdx) {
    isSurfaceVoxel[voxelAIdx] = 1;
    isSurfaceVoxel[voxelBIdx] = 1;

    faceConstraints[constraintIdx].voxelAIdx = -1;
    faceConstraints[constraintIdx].voxelBIdx = -1;
}

void weakenLongRangeConstraints(uint voxelAParticlesIdx, uint voxelBParticlesIdx) {
    [unroll]
    for (int i = 0; i < 4; ++i) {
        uint particleAIdx = voxelAParticlesIdx + faceAParticles[i];
        uint particleBIdx = voxelBParticlesIdx + faceBParticles[i];

        // This looks a bit strange, but basically we're using the longRangeConstraintIndices array for two purposes:
        // The higher bits store the index of the long-range constraint associated with the particle.
        // The lower 4 bits of the longRangeConstraintIndices serve as a counter for how many face constraints within 
        // the constraint's voxel grouping have been broken. 
        // 
        // Since there are at most 12 face constraints per voxel grouping,
        // we can safely increment without worrying about overflow into the higher bits (the constraint indices).
        // 
        // The inner access into longRangeConstraintIndices gets us the long range constraint index associated with this particle,
        // and the outer access uses it to increment the number of broken faces for that constraint, stored at a different location, per-constraint, in the same array. 
        //
        // Also, we denote a particle with no long-range constraint by the sentinel value of 0xFFFFFFF0 in the higher bits (0x0FFFFFFF after shifting right by 4).
        uint longRangeConstraintIdxA = longRangeConstraintIndices[particleAIdx] >> 4;
        uint longRangeConstraintIdxB = longRangeConstraintIndices[particleBIdx] >> 4;

        if (longRangeConstraintIdxA != 0x0FFFFFFF) {
            InterlockedAdd(longRangeConstraintIndices[longRangeConstraintIdxA], 1);
        }
        if (longRangeConstraintIdxB != 0x0FFFFFFF) {
            InterlockedAdd(longRangeConstraintIndices[longRangeConstraintIIdxB], 1);
        }
    }
}

/**
* Solves face constraints for a pair of voxels using the VGS method.
* One thread = one face constraint. 
*/
[numthreads(VGS_THREADS, 1, 1)]
void main(
    uint3 globalThreadId : SV_DispatchThreadID
)
{
    uint constraintIdx = globalThreadId.x;
    if (constraintIdx >= numConstraints) return;

    FaceConstraint constraint;
    constraint = faceConstraints[constraintIdx];
    
    // A face constraint deals with two voxels, which we'll refer to as A and B throughout this shader.
    int voxelAIdx = constraint.voxelAIdx;
    int voxelBIdx = constraint.voxelBIdx;

    // Face constraint is already broken.
    if (voxelAIdx == -1 || voxelBIdx == -1) return;

    uint voxelAParticlesIdx = voxelAIdx << 3;
    uint voxelBParticlesIdx = voxelBIdx << 3;

    Particle voxelParticles[8];
    bool faceConstraintBroke = false;
    for (int i = 0; i < 4; ++i) {
        // Note: this is NOT a mistake. The face indices of the opposite voxel tell us where to index
        // the particles of the first voxel. Each face*Particles array tells us which particles to use from each voxel,
        // but we leverage the order within each array to avoid axis-specific control flow.
        voxelParticles[faceBParticles[i]] = particles[voxelAParticlesIdx + faceAParticles[i]];
        voxelParticles[faceAParticles[i]] = particles[voxelBParticlesIdx + faceBParticles[i]];

        // Check if the constraint between these two voxels should be broken due to tension/compression
        float edgeLength = length(voxelParticles[faceAParticles[i]].position - voxelParticles[faceBParticles[i]].position);
        float strain = (edgeLength - 2.0f * vgsConstants.particleRadius) / (2.0f * vgsConstants.particleRadius);
        if (strain > constraint.tensionLimit || strain < constraint.compressionLimit) {
            breakConstraint(constraintIdx, voxelAIdx, voxelBIdx);
            faceConstraintBroke = true;
            break;
        }
    }

    if (faceConstraintBroke) {
        weakenLongRangeConstraints(voxelAParticlesIdx, voxelBParticlesIdx);
        return;
    }

    // Now we do VGS iterations on the imaginary "voxel" formed by the particles of the two voxels' faces.
    doVGSIterations(voxelParticles, vgsConstants, true);

    // Write back the updated particles to global memory
    for (int j = 0; j < 4; ++j) {
        // Again, the mixing of A and B is actually not a mistake. It's a result of how we defined the face indices,
        // taking advantage of the ordering to be able to write one shader for all axes with no branching.
        if (!massIsInfinite(voxelParticles[j])) {
            particles[voxelAParticlesIdx + faceAParticles[j]] = voxelParticles[faceBParticles[j]];
        }
        if (!massIsInfinite(voxelParticles[j])) {
            particles[voxelBParticlesIdx + faceBParticles[j]] = voxelParticles[faceAParticles[j]];
        }
    }
}