#include "common.hlsl"
#include "constants.hlsli"
#include "faceconstraints_shared.hlsl"

[numthreads(VGS_THREADS, 1, 1)]
void main(
    uint3 globalThreadId : SV_DispatchThreadID
) {
    uint constraintIdx = globalThreadId.x;
    if (constraintIdx >= numConstraints) return;

    FaceConstraint constraint = faceConstraints[constraintIdx];
    int voxelAIdx = constraint.voxelAIdx;
    int voxelBIdx = constraint.voxelBIdx;
    if (voxelAIdx == -1 || voxelBIdx == -1) return;

    uint voxelAStartIdx = voxelAIdx << 3;
    uint voxelBStartIdx = voxelBIdx << 3;
  
    for (uint i = 0; i < 4; i++) {
        Particle particleA = renderParticles[voxelAStartIdx + faceAParticles[i]];
        Particle particleB = renderParticles[voxelBStartIdx + faceBParticles[i]];

        float3 particleAToB = particleB.position - particleA.position;
        float distSq = dot(particleAToB, particleAToB);
        float dist = sqrt(distSq);
        float3 correction = (dist - 2.0f * vgsConstants.particleRadius) * (particleAToB / dist) * 0.55f;

        particleA.position += correction;
        particleB.position -= correction;

        renderParticles[voxelAStartIdx + faceAParticles[i]] = particleA;
        renderParticles[voxelBStartIdx + faceBParticles[i]] = particleB;
    }
}