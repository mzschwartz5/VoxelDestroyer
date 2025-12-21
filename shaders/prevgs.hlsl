#include "common.hlsl"
#include "prevgs_shared.hlsl"

[numthreads(VGS_THREADS, 1, 1)]
void main(uint3 gId : SV_DispatchThreadID) 
{
    if (gId.x >= preVgsConstants.numParticles) return; 
    
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
        delta += float4(0, preVgsConstants.gravityStrength, 0, 0) * preVgsConstants.timeStep * preVgsConstants.timeStep;
        pos.xyz += delta.xyz;
    }
    
    // Write back to global memory
    positions[gId.x] = pos;
}

