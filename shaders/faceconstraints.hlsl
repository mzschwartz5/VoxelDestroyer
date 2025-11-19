#include "vgs_core.hlsl"

// Face constraint structure
struct FaceConstraint {
    int voxelAIdx;
    int voxelBIdx;
    float tensionLimit;
    float compressionLimit;
};

cbuffer VoxelSimBuffer : register(b0)
{
    float RELAXATION;
    float BETA;
    float PARTICLE_RADIUS;
    float VOXEL_REST_VOLUME;
    float ITER_COUNT;
    float FTF_RELAXATION;
    float FTF_BETA;
    int NUM_VOXELS;
};

cbuffer FaceConstraintsCB : register(b1)
{
    uint4 faceAParticles; // Which particles of voxel A are involved in the face constraint
    uint4 faceBParticles; // Which particles of voxel B are involved in the face constraint
    uint numConstraints;
    int faceAId;          // Which face index this constraint corresponds to on voxel A (only used for paint value lookup)
    int faceBId;          // Which face index this constraint corresponds to on voxel B (only used for paint value lookup)
    int padding0;
};

RWStructuredBuffer<float4> positions : register(u0);
RWStructuredBuffer<FaceConstraint> faceConstraints : register(u1);
RWStructuredBuffer<uint> isSurfaceVoxel : register(u2);

void breakConstraint(int constraintIdx, int voxelAIdx, int voxelBIdx) {
    isSurfaceVoxel[voxelAIdx] = 1;
    isSurfaceVoxel[voxelBIdx] = 1;

    faceConstraints[constraintIdx].voxelAIdx = -1;
    faceConstraints[constraintIdx].voxelBIdx = -1;
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

    float4 pos[8];
    for (int i = 0; i < 4; ++i) {
        // Note: this is NOT a mistake. The face indices of the opposite voxel tell us where to index
        // the particles of the first voxel. Each face*Particles array tells us which particles to use from each voxel,
        // but we leverage the order within each array to avoid axis-specific control flow.
        pos[faceBParticles[i]] = positions[voxelAParticlesIdx + faceAParticles[i]];
        pos[faceAParticles[i]] = positions[voxelBParticlesIdx + faceBParticles[i]];

        // Check if the constraint between these two voxels should be broken due to tension/compression
        float edgeLength = length(pos[faceAParticles[i]].xyz - pos[faceBParticles[i]].xyz);
        float strain = (edgeLength - 2.0f * PARTICLE_RADIUS) / (2.0f * PARTICLE_RADIUS);
        if (strain > constraint.tensionLimit || strain < constraint.compressionLimit) {
            breakConstraint(constraintIdx, voxelAIdx, voxelBIdx);
            return;
        }
    }

    // Now we do VGS iterations on the imaginary "voxel" formed by the particles of the two voxels' faces.
    doVGSIterations(
        pos,
        PARTICLE_RADIUS,
        VOXEL_REST_VOLUME,
        ITER_COUNT,
        FTF_RELAXATION,
        FTF_BETA,
        true
    );

    // Write back the updated positions to global memory
    for (int j = 0; j < 4; ++j) {
        // Again, the mixing of A and B is actually not a mistake. It's a result of how we defined the face indices,
        // taking advantage of the ordering to be able to write one shader for all axes with no branching.
        if (!massIsInfinite(pos[j])) {
            positions[voxelAParticlesIdx + faceAParticles[j]] = pos[faceBParticles[j]];
        }
        if (!massIsInfinite(pos[j])) {
            positions[voxelBParticlesIdx + faceBParticles[j]] = pos[faceAParticles[j]];
        }
    }
}

RWBuffer<float> paintDeltas : register(u3);

// This entry point is for updating the face constraints based on paint values.
// One thread per face constraint (over three dispatches, one per axis - just because that's how the face constraint data is stored).
// This runs every time a brush stroke ends - that way, we can use the computed delta to know which faces were updated, and can 
// update the neighboring face in the constraint pair. (And it also means the simulation values are always in sync).
[numthreads(VGS_THREADS, 1, 1)]
void updateFaceConstraintsFromPaint(
    uint3 globalThreadId : SV_DispatchThreadID
)
{
    uint constraintIdx = globalThreadId.x;
    if (constraintIdx >= numConstraints) return;

    FaceConstraint constraint;
    constraint = faceConstraints[constraintIdx];

    int voxelAIdx = constraint.voxelAIdx;
    int voxelBIdx = constraint.voxelBIdx;
    if (voxelAIdx == -1 || voxelBIdx == -1) return;

    int paintValueAIdx = voxelAIdx * 6 + faceAId;
    int paintValueBIdx = voxelBIdx * 6 + faceBId;

    // In theory, in a given brush stroke, the user can only paint one side of a face constraint. (If camera-based painting is off,
    // they can get both sides, but they would be the same value). So we take whichever value is non-zero (if either), and set the constraint limits
    // based on that. And also set the other paint value in the pair to the same value so both sides are visually painted consistent.
    float paintDeltaA = paintDeltas[paintValueAIdx];
    float paintDeltaB = paintDeltas[paintValueBIdx];
    if (paintDeltaA < eps && paintDeltaB < eps) return;

    // Branch-free selection of the paint delta to use (pick B if non-zero, else A)
    float useB = abs(sign(paintDeltaB));
    float selectedDelta = paintDeltaA + useB * (paintDeltaB - paintDeltaA);
    int unselectedIdx = paintValueBIdx + (int)(useB) * (paintValueAIdx - paintValueBIdx);

    paintDeltas[unselectedIdx] = selectedDelta;

    // For now, same paint value for both limits
    constraint.tensionLimit = selectedDelta;
    constraint.compressionLimit = -selectedDelta;
    faceConstraints[constraintIdx] = constraint;
}