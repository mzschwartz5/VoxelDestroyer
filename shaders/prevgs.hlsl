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

    int voxelIndex = gId.x >> 3;
    if (isDragging[voxelIndex]) return;

    float4 delta = (pos - oldPos);
    delta += float4(0, preVgsConstants.gravityStrength, 0, 0) * preVgsConstants.timeStep * preVgsConstants.timeStep;
    pos.xyz += delta.xyz;
    
    // Write back to global memory
    positions[gId.x] = pos;
}

