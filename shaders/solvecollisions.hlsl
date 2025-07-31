#include "particle_collisions_shared.hlsl"

StructuredBuffer<uint> particleIndices : register(t0);
StructuredBuffer<uint> collisionCellParticleCounts : register(t1);
RWStructuredBuffer<float4> particles : register(u0);

bool doParticlesOverlap(float4 particleA, float4 particleB, out float distanceSquared, out float3 particleAToB)
{
    particleAToB = particleB.xyz - particleA.xyz;
    distanceSquared = dot(particleAToB, particleAToB);
    if (distanceSquared < 1e-6f) return false; // Avoid division by zero or NaN
    return (distanceSquared < (4.0f * particleRadius * particleRadius));
}

// Max out shared memory. See note below about how many particles each thread can store, based
// on the number of threads per workgroup and how many threads are assigned to each cell. (And see constants.h for SOLVE_COLLISION_THREADS).
#define SHARED_MEMORY_SIZE 2048
groupshared float4 s_particles[SHARED_MEMORY_SIZE];

/**
 * Resolve collisions between particles in the same collision cell. (Particles have been pre-binned into all cells they overlap)
*/
[numthreads(SOLVE_COLLISION_THREADS, 1, 1)]
void main(uint3 globalId : SV_DispatchThreadID, uint3 groupThreadId : SV_GroupThreadID) {
    if (globalId.x >= hashGridSize) return;

    uint particleStartIdx = collisionCellParticleCounts[globalId.x];
    uint particleEndIdx = collisionCellParticleCounts[globalId.x + 1]; // No out of bounds concern because we added a guard (extra buffer entry) for this very purpose.
    uint sharedMemoryStartIdx = particleStartIdx - collisionCellParticleCounts[globalId.x - groupThreadId.x];

    // Store particles in shared memory.
    uint numParticlesInCell = particleEndIdx - particleStartIdx;
    for (uint u = 0; u < numParticlesInCell; ++u) {
        if (sharedMemoryStartIdx + u >= SHARED_MEMORY_SIZE) continue; // Ignore any particles that would overflow shared memory.
        s_particles[sharedMemoryStartIdx + u] = particles[particleIndices[particleStartIdx + u]];
    }

    for (uint i = 0; i < numParticlesInCell; ++i) {
        if (sharedMemoryStartIdx + i >= SHARED_MEMORY_SIZE) continue;
        for (uint j = i + 1; j < numParticlesInCell; ++j) {
            if (sharedMemoryStartIdx + j >= SHARED_MEMORY_SIZE) continue;

            float4 particleA = s_particles[sharedMemoryStartIdx + i];
            float4 particleB = s_particles[sharedMemoryStartIdx + j];

            float distanceSquared;
            float3 particleAToB;
            if (!doParticlesOverlap(particleA, particleB, distanceSquared, particleAToB)) continue;

            // TODO: augment with the voxel normals to avoid interlock.
            // TODO: take particle mass into account.
            float3 delta = normalize(particleAToB) * (2.0f * particleRadius - sqrt(distanceSquared));
            s_particles[sharedMemoryStartIdx + i].xyz -= delta * 0.5f;
            s_particles[sharedMemoryStartIdx + j].xyz += delta * 0.5f;
        }
    }

    // Write the particles back to global memory.
    for (uint v = 0; v < numParticlesInCell; ++v) {
        if (sharedMemoryStartIdx + v >= SHARED_MEMORY_SIZE) continue; // Ignore any particles that would overflow shared memory.
        particles[particleIndices[particleStartIdx + v]] = s_particles[sharedMemoryStartIdx + v];
    }
}