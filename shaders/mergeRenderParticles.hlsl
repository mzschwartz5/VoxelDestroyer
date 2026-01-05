#include "common.hlsl"
#include "constants.hlsli"
#include "faceconstraints_shared.hlsl"

RWStructuredBuffer<Particle> renderParticles : register(u4);

[numthreads(VGS_THREADS, 1, 1)]
void main(
    uint3 globalThreadId : SV_DispatchThreadID
) {
    uint constraintIdx = globalThreadId.x;
    if (constraintIdx >= numConstraints) return;

    int voxelAIdx = faceConstraintsIndices[constraintIdx * 2];
    int voxelBIdx = faceConstraintsIndices[constraintIdx * 2 + 1];
    if (voxelAIdx == -1 || voxelBIdx == -1) return;

    uint voxelAStartIdx = voxelAIdx << 3;
    uint voxelBStartIdx = voxelBIdx << 3;
  
    for (uint i = 0; i < 4; i++) {
        Particle particleA = renderParticles[voxelAStartIdx + faceAParticles[i]];
        Particle particleB = renderParticles[voxelBStartIdx + faceBParticles[i]];

        // Note: should *not* be weighted by particle mass (or else all 4 particles will not end up in same spot)
        float3 midPosition = 0.5f * (particleA.position + particleB.position);
        particleA.position = midPosition;
        particleB.position = midPosition;

        renderParticles[voxelAStartIdx + faceAParticles[i]] = particleA;
        renderParticles[voxelBStartIdx + faceBParticles[i]] = particleB;
    }
}