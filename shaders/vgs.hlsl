#include "vgs_core.hlsl"
#include "constants.hlsli"

RWStructuredBuffer<float4> positions : register(u0);

cbuffer VoxelSimBuffer : register(b0)
{
    float RELAXATION;
    float BETA;
    float PARTICLE_RADIUS;
    float VOXEL_REST_VOLUME;
    float ITER_COUNT;
    float PADDING_0;
    float PADDING_1;
    uint NUM_VOXELS;
};

[numthreads(VGS_THREADS, 1, 1)]
void main(
    uint3 globalThreadId : SV_DispatchThreadID,
    uint3 groupId : SV_GroupID,
    uint3 localThreadId : SV_GroupThreadID
)
{
    uint voxel_idx = globalThreadId.x;
    if (voxel_idx >= NUM_VOXELS) return;

    uint start_idx = voxel_idx << 3;
    
    float4 pos[8];
    for (int i = 0; i < 8; ++i) {
        pos[i] = positions[start_idx + i];
    }

    // Perform VGS iterations
    doVGSIterations(
        pos,
        PARTICLE_RADIUS,
        VOXEL_REST_VOLUME,
        ITER_COUNT,
        RELAXATION,
        BETA,
        false
    );

    // Write back the updated positions
    for (int j = 0; j < 8; ++j) {
        if (massIsInfinite(pos[j])) continue;
        positions[start_idx + j] = pos[j];
    }

}