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
    float padding;
};

cbuffer FaceIndicesBuffer : register(b1)
{
    uint4 faceAParticles;
    uint4 faceBParticles;
};

RWStructuredBuffer<float4> positions : register(u0);
RWStructuredBuffer<FaceConstraint> faceConstraints : register(u1);
RWStructuredBuffer<uint> isSurfaceVoxel : register(u2);
StructuredBuffer<float> weights : register(t0);

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
    uint3 globalThreadId : SV_DispatchThreadID,
    uint3 groupId : SV_GroupID,
    uint3 localThreadId : SV_GroupThreadID
)
{
    uint constraintIdx = globalThreadId.x;
    FaceConstraint constraint;
    constraint = faceConstraints[constraintIdx];
    
    // A face constraint deals with two voxels, which we'll refer to as A and B throughout this shader.
    int voxelAIdx = constraint.voxelAIdx;
    int voxelBIdx = constraint.voxelBIdx;

    // Face constraint is already broken.
    if (voxelAIdx == -1 || voxelBIdx == -1) return;

    uint voxelAParticlesIdx = voxelAIdx << 3;
    uint voxelBParticlesIdx = voxelBIdx << 3;

    float3 pos[8];
    float w[8];
    for (int i = 0; i < 4; ++i) {
        // Note: this is NOT a mistake. The face indices of the opposite voxel tell us where to index
        // the particles of the first voxel. Each face*Particles array tells us which particles to use from each voxel,
        // but we leverage the order within each array to avoid axis-specific control flow.
        pos[faceBParticles[i]] = positions[voxelAParticlesIdx + faceAParticles[i]].xyz;
        pos[faceAParticles[i]] = positions[voxelBParticlesIdx + faceBParticles[i]].xyz;

        // Check if the constraint between these two voxels should be broken due to tension/compression
        float3 edgeLength = length(pos[faceAParticles[i]] - pos[faceBParticles[i]]);
        float strain = (edgeLength - 2.0f * PARTICLE_RADIUS) / (2.0f * PARTICLE_RADIUS);
        if (strain > constraint.tensionLimit || strain < constraint.compressionLimit) {
            breakConstraint(constraintIdx, voxelAIdx, voxelBIdx);
            return;
        }

        w[faceBParticles[i]] = weights[voxelAParticlesIdx + faceAParticles[i]];
        w[faceAParticles[i]] = weights[voxelBParticlesIdx + faceBParticles[i]];
    }

    // Now we do VGS iterations on the imaginary "voxel" formed by the particles of the two voxels' faces.
    bool constraintIntact = doVGSIterations(
        pos,
        w,
        PARTICLE_RADIUS,
        VOXEL_REST_VOLUME,
        ITER_COUNT,
        FTF_RELAXATION,
        FTF_BETA
    );

    if (!constraintIntact) {
        breakConstraint(constraintIdx, voxelAIdx, voxelBIdx);
        return;
    }

    // Write back the updated positions to global memory
    for (int i = 0; i < 4; ++i) {
        // Again, the mixing of A and B is actually not a mistake. It's a result of how we defined the face indices,
        // taking advantage of the ordering to be able to write one shader for all axes with no branching.
        if (w[i] != 0.0f) {
            positions[voxelAParticlesIdx + faceAParticles[i]] = float4(pos[faceBParticles[i]], 1.0f);
        }
        if (w[i] != 0.0f) {
            positions[voxelBParticlesIdx + faceBParticles[i]] = float4(pos[faceAParticles[i]], 1.0f);
        }
    }
}