#include "vgs_core.hlsl"

StructuredBuffer<uint> longRangeParticleIndices : register(t0);
RWStructuredBuffer<Particle> particles : register(u0);

cbuffer LongRangeConstraintsCB : register(b0)
{
    uint numConstraints;
    uint padding0;
    uint padding1;
    uint padding2;
};

cbuffer VGSConstantsCB : register(b1)
{
    VGSConstants vgsConstants;
};

bool longRangeConstraintBroken(uint particleIdx0) {
    // The lower 4 bits (0xF) of the first particle are a counter of how many face constraints associated with this long-range constraint have been broken.
    // Three is a bit of a heuristic: it's the minimum number of face constraints internal to a 2x2x2 voxel grouping
    // that may disconnect the group into multiple parts. If that many (or more) are broken, we must break the long-range constraint.
    return (particleIdx0 & 0xF) >= 3u;
}

[numthreads(VGS_THREADS, 1, 1)]
void main(uint3 globalThreadId : SV_DispatchThreadID)
{
    uint constraintIdx = globalThreadId.x;
    if (constraintIdx >= numConstraints) {
        return;
    }

    uint particleIdx0 = longRangeParticleIndices[constraintIdx << 3];
    if (longRangeConstraintBroken(particleIdx0)) return;
      
    uint particleIndices[8];
    Particle constraintParticles[8];

    particleIndices[0] = particleIdx0 >> 4;
    constraintParticles[0] = particles[particleIndices[0]];

    [unroll] for (uint i = 1; i < 8; ++i) {
        particleIndices[i] = longRangeParticleIndices[(constraintIdx << 3) + i] >> 4;
        constraintParticles[i] = particles[particleIndices[i]];
    }

    doVGSIterations(constraintParticles, vgsConstants, true);

    [unroll] for (uint j = 0; j < 8; ++j) {
        uint particleIdx = particleIndices[j];
        particles[particleIdx] = constraintParticles[j];
    }
}