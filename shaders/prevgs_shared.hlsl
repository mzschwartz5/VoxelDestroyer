StructuredBuffer<bool> isDragging : register(t0);
RWStructuredBuffer<float4> positions : register(u0);
RWStructuredBuffer<float4> oldPositions : register(u1);
RWBuffer<float> paintDeltas : register(u2);
RWBuffer<float> paintValues : register(u3);

cbuffer VoxelSimBuffer : register(b0)
{
    float GRAVITY_STRENGTH;
    float GROUND_Y;
    float TIMESTEP;
    uint numParticles;
    float massLow;
    float massHigh;
    int padding0;
    int padding1;
};