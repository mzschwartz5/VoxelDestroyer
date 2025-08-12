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
};

[numthreads(VGS_THREADS, 1, 1)]
void main(uint3 gId : SV_DispatchThreadID) 
{
    if (gId.x >= numParticles) return; 
    
    float4 pos = positions[gId.x];
    if (massIsInfinite(pos)) return;

    float4 oldPos = oldPositions[gId.x];
    oldPositions[gId.x] = pos;

    float4 velocity = (pos - oldPos) / TIMESTEP;
    
    int voxelIndex = gId.x >> 3;
    if (!isDragging[voxelIndex]) {
        velocity += float4(0, GRAVITY_STRENGTH, 0, 0) * TIMESTEP; // Gravity
        pos.xyz += (velocity * TIMESTEP).xyz; // Update position
    }

    // For now, lump ground collision into this shader
    if (pos.y < GROUND_Y) {
        pos = oldPos;
        pos.y = GROUND_Y;
    }

    // Write back to global memory
    positions[gId.x] = pos;
}