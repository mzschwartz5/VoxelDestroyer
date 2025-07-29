#include "particle_collisions_shared.hlsl"

StructuredBuffer<float4> particles : register(t0);
RWStructuredBuffer<uint> particleIndices : register(u0);

[numthreads(SOLVE_COLLISION_THREADS, 1, 1)]
void main(uint3 gId : SV_DispatchThreadID) {
    
}