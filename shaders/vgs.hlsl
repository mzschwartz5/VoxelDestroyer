#include "vgs_core.hlsl"

RWStructuredBuffer<Particle> particles : register(u0);

cbuffer VGSConstantBuffer : register(b0)
{
    VGSConstants vgsConstants;
};

[numthreads(VGS_THREADS, 1, 1)]
void main(
    uint3 globalThreadId : SV_DispatchThreadID,
    uint3 groupId : SV_GroupID,
    uint3 localThreadId : SV_GroupThreadID
)
{
    uint voxel_idx = globalThreadId.x;
    if (voxel_idx >= vgsConstants.numVoxels) return;

    uint start_idx = voxel_idx << 3;
    
    Particle voxelParticles[8];
    for (int i = 0; i < 8; ++i) {
        voxelParticles[i] = particles[start_idx + i];
    }

    doVGSIterations(voxelParticles, vgsConstants, false);

    // Write back the updated particles
    for (int j = 0; j < 8; ++j) {
        if (massIsInfinite(voxelParticles[j])) continue;
        particles[start_idx + j] = voxelParticles[j];
    }

}