#include "particle_collisions_shared.hlsl"

StructuredBuffer<uint> particleIndices : register(t0);
StructuredBuffer<uint> collisionCellParticleCounts : register(t1);
RWStructuredBuffer<float4> particles : register(u0);

bool doParticlesOverlap(float3 particleA, float3 particleB, out float distanceSquared, out float3 particleAToB)
{
    particleAToB = particleB - particleA;
    distanceSquared = dot(particleAToB, particleAToB);
    if (distanceSquared < 1e-6f) return false; // Avoid division by zero or NaN
    return (distanceSquared < 4.0f * particleRadius * particleRadius);
}

bool doVoxelCentersOverlap(float3 voxelACenter, float3 voxelBCenter, out float3 voxelAToB)
{
    voxelAToB = voxelBCenter - voxelACenter;
    float distanceSquared = dot(voxelAToB, voxelAToB);
    if (distanceSquared < 1e-6f) return false; // Avoid division by zero or NaN
    return (distanceSquared < 9.0f * particleRadius * particleRadius);
}

// Approximates the center of a voxel that owns a particle by the average of the particle and its diagonal particle in the voxel.
// The diagonal particle is retrieved using index relationships between different particles in the voxel (by construction).
float3 getVoxelCenterOfParticle(float3 particle, uint localParticleIdx) {
    int particleGlobalIdx = particleIndices[localParticleIdx];
    int voxelIdx = particleGlobalIdx >> 3;
    int particleIdxInVoxel = (particleGlobalIdx - (voxelIdx << 3));
    int particleDiagIdx = (voxelIdx << 3) + 7 - particleIdxInVoxel;
    float3 particleDiag = particles[particleDiagIdx].xyz;
    return (particle + particleDiag) * 0.5f;
}

// Max out shared memory. See note below about how many particles each thread can store, based
// on the number of threads per workgroup and how many threads are assigned to each cell. (And see constants.h for SOLVE_COLLISION_THREADS).
#define SHARED_MEMORY_SIZE 1638 // maximum number of (float4 + bool)'s can fit in 32KB of shared memory
groupshared float4 s_particles[SHARED_MEMORY_SIZE];
groupshared bool s_positionChanged[SHARED_MEMORY_SIZE];

/**
 * Resolve collisions between particles in the same collision cell. (Particles have been pre-binned into all cells they overlap)
 * Note: no shared memory barriers are needed because each thread writes to its own section of shared memory. (so "shared" is a bit of a misnomer here :D)
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
        s_positionChanged[sharedMemoryStartIdx + u] = false; // Initialize position changed flags.
    }

    for (uint i = 0; i < numParticlesInCell; ++i) {
        if (sharedMemoryStartIdx + i >= SHARED_MEMORY_SIZE) continue;
        for (uint j = i + 1; j < numParticlesInCell; ++j) {
            if (sharedMemoryStartIdx + j >= SHARED_MEMORY_SIZE) continue;

            float4 particleA = s_particles[sharedMemoryStartIdx + i];
            float4 particleB = s_particles[sharedMemoryStartIdx + j];

            float distanceSquared;
            float3 particleAToB;
            if (!doParticlesOverlap(particleA.xyz, particleB.xyz, distanceSquared, particleAToB)) continue;
            particleAToB = normalize(particleAToB);

            // Get the particles diagonal to A and B within their respective voxels.
            // Then approximate the voxel centers to augment collision normals, to avoid voxel interlock.
            float3 voxelACenter = getVoxelCenterOfParticle(particleA.xyz, particleStartIdx + i);
            float3 voxelBCenter = getVoxelCenterOfParticle(particleB.xyz, particleStartIdx + j);

            float delta = (2.0f * particleRadius - sqrt(distanceSquared));
    
            float3 augmentedNormal = particleAToB;
            float3 voxelAToB;
            if (doVoxelCentersOverlap(voxelACenter, voxelBCenter, voxelAToB)) {
                augmentedNormal = normalize(normalize(voxelAToB) + particleAToB);
            }
            
            // TODO: take particle mass into account
            // Factor of 0.35 is somewhat arbitrary. 0.5 would be inifinite stiffness - so this is a little more compliant.
            s_particles[sharedMemoryStartIdx + i].xyz -= delta * 0.35f * augmentedNormal;
            s_particles[sharedMemoryStartIdx + j].xyz += delta * 0.35f * augmentedNormal;
            s_positionChanged[sharedMemoryStartIdx + i] = true;
            s_positionChanged[sharedMemoryStartIdx + j] = true;
        }
    }

    // Write the particles back to global memory.
    for (uint v = 0; v < numParticlesInCell; ++v) {
        if (sharedMemoryStartIdx + v >= SHARED_MEMORY_SIZE) continue; // Ignore any particles that would overflow shared memory.
        if (!s_positionChanged[sharedMemoryStartIdx + v]) continue;
        particles[particleIndices[particleStartIdx + v]] = s_particles[sharedMemoryStartIdx + v];
    }
}