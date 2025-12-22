#include "constants.hlsli"

StructuredBuffer<bool> isDragging : register(t0);
RWStructuredBuffer<Particle> particles : register(u0);
RWStructuredBuffer<Particle> oldParticles : register(u1);
RWBuffer<float> paintDeltas : register(u2);
RWBuffer<float> paintValues : register(u3);

cbuffer PreVGSConstantBuffer : register(b0)
{
    PreVGSConstants preVgsConstants;
};