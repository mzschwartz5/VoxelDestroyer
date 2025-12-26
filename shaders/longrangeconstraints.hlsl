#include "vgs_core.hlsl"

StructuredBuffer<uint> longRangeParticleIndices : register(t0);
StructuredBuffer<uint> longRangeIndicesAndCounters : register(t1);
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

[numthreads(VGS_THREADS, 1, 1)]
void main(uint3 globalThreadId : SV_DispatchThreadID)
{
    uint constraintIdx = globalThreadId.x;
    if (constraintIdx >= numConstraints) {
        return;
    }

    Particle constraintParticles[8];
    uint particleIndices[8];
    [unroll] for (uint i = 0; i < 8; ++i) {
        particleIndices[i] = longRangeParticleIndices[(constraintIdx << 3) + i];
        constraintParticles[i] = particles[particleIndices[i]];
    }

    doVGSIterations(constraintParticles, vgsConstants, true);

    [unroll] for (uint j = 0; j < 8; ++j) {
        uint particleIdx = particleIndices[j];
        particles[particleIdx] = constraintParticles[j];
    }
}