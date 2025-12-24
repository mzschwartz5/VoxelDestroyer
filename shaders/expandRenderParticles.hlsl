#include "common.hlsl"
#include "constants.hlsli"
#include "faceconstraints_shared.hlsl"

[numthreads(VGS_THREADS, 1, 1)]
void main(
    uint3 globalThreadId : SV_DispatchThreadID
) {
    uint voxelIdx = globalThreadId.x;
    if (voxelIdx >= vgsConstants.numVoxels) return;

    uint base = voxelIdx << 3;
    Particle p0 = particles[base + 0];
    Particle p1 = particles[base + 1];
    Particle p2 = particles[base + 2];
    Particle p3 = particles[base + 3];
    Particle p4 = particles[base + 4];
    Particle p5 = particles[base + 5];
    Particle p6 = particles[base + 6];
    Particle p7 = particles[base + 7];

    // Approximate voxel basis (assumes parallelepiped shape, which is an approximation)
    float voxelRestLengthInv = 1.0f / (2.0f * vgsConstants.particleRadius);
    float3 v0 = (p1.position - p0.position) * voxelRestLengthInv;
    float3 v1 = (p2.position - p0.position) * voxelRestLengthInv;
    float3 v2 = (p4.position - p0.position) * voxelRestLengthInv;
    
    float particleRadius = vgsConstants.particleRadius;
    renderParticles[base + 0].position = p0.position + particleRadius * (-v0 - v1 - v2);
    renderParticles[base + 1].position = p1.position + particleRadius * ( v0 - v1 - v2);
    renderParticles[base + 2].position = p2.position + particleRadius * (-v0 + v1 - v2);
    renderParticles[base + 3].position = p3.position + particleRadius * ( v0 + v1 - v2);
    renderParticles[base + 4].position = p4.position + particleRadius * (-v0 - v1 + v2);
    renderParticles[base + 5].position = p5.position + particleRadius * ( v0 - v1 + v2);
    renderParticles[base + 6].position = p6.position + particleRadius * (-v0 + v1 + v2);
    renderParticles[base + 7].position = p7.position + particleRadius * ( v0 + v1 + v2);
}