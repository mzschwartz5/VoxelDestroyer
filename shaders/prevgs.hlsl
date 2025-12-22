#include "common.hlsl"
#include "prevgs_shared.hlsl"

[numthreads(VGS_THREADS, 1, 1)]
void main(uint3 gId : SV_DispatchThreadID) 
{
    if (gId.x >= preVgsConstants.numParticles) return; 
    
    Particle particle = particles[gId.x];
    if (massIsInfinite(particle)) return;

    Particle oldParticle = oldParticles[gId.x];
    oldParticles[gId.x] = particle;

    int voxelIndex = gId.x >> 3;
    if (isDragging[voxelIndex]) return;

    float3 delta = (particle.position - oldParticle.position);
    delta += float3(0, preVgsConstants.gravityStrength, 0) * preVgsConstants.timeStep * preVgsConstants.timeStep;
    particle.position += delta;
    
    // Write back to global memory
    particles[gId.x] = particle;
}

