#include "common.hlsl"

StructuredBuffer<bool> isDragging : register(t0);
RWStructuredBuffer<float4> positions : register(u0);
RWStructuredBuffer<float4> oldPositions : register(u1);

cbuffer VoxelSimBuffer : register(b0)
{
    float GRAVITY_STRENGTH;
    float GROUND_Y;
    float TIMESTEP;
    int numParticles;
    float massLow;
    float massHigh;
    int padding0;
    int padding1;
};

[numthreads(VGS_THREADS, 1, 1)]
void main(uint3 gId : SV_DispatchThreadID) 
{
    if (gId.x >= numParticles) return; 
    
    float4 pos = positions[gId.x];
    if (massIsInfinite(pos)) return;

    float4 oldPos = oldPositions[gId.x];
    oldPositions[gId.x] = pos;

    // Note: delay applying timestep (i.e. don't calculate velocity now) because:
    // 1. Division is expensive
    // 2. Makes delta rounding errors worse when timestep is small
    float4 delta = (pos - oldPos);
    
    int voxelIndex = gId.x >> 3;
    if (!isDragging[voxelIndex]) {
        delta += float4(0, GRAVITY_STRENGTH, 0, 0) * TIMESTEP * TIMESTEP; // Gravity
        pos.xyz += delta.xyz; // Update position
    }
    
    // Write back to global memory
    positions[gId.x] = pos;
}

RWBuffer<float> paintDeltas : register(u2);
RWBuffer<float> paintValues : register(u3);

// Second entry point in this shader, used for updating particle (inverse) masses based on paint values
// Users paint particle _mass_, so we also need to convert to inverse mass here, 
// treating negative paint values (indicating infinite mass) as 0 inverse mass (the convention for infinite mass in the simulation)
// One thread per particle.
[numthreads(VGS_THREADS, 1, 1)]
void updateParticleMassFromPaint(uint3 gId : SV_DispatchThreadID) {
    if (gId.x >= numParticles) return;

    float paintDelta = paintDeltas[gId.x];
    if (abs(paintDelta) < 1e-6) return;

    float paintValue = paintValues[gId.x];
    float packedRadiusAndInvMass = positions[gId.x].w;

    if (paintValue < 0.0f) { 
        // Infinite mass case.
        packedRadiusAndInvMass = updateMass(packedRadiusAndInvMass, 0.0f);
    } else {
        // Note: we prevent the user from setting a mass lower limit of 0, so no need to check for division by zero here.
        float mass = lerp(massLow, massHigh, paintValue);
        float invMass = 1.0f / mass;
        packedRadiusAndInvMass = updateMass(packedRadiusAndInvMass, invMass);
    }

    positions[gId.x].w = packedRadiusAndInvMass;
}