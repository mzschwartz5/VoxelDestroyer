#include "particle_collisions_shared.hlsl"
#include "common.hlsl"

StructuredBuffer<uint> particleIndices : register(t0);
StructuredBuffer<uint> collisionCellParticleCounts : register(t1);
RWStructuredBuffer<float4> particles : register(u0);

bool doParticlesOverlap(
    float3 particleA,
    float3 particleB,
    float particleARadius,
    float particleBRadius,
    out float distanceSquared, 
    out float3 particleAToB
)
{
    particleAToB = particleB - particleA;
    distanceSquared = dot(particleAToB, particleAToB);
    if (distanceSquared < 1e-6f) return false; // Avoid division by zero or NaN

    return (distanceSquared < (particleARadius + particleBRadius) * (particleARadius + particleBRadius));
}

// Approximates the center of a voxel that owns a particle by the average of the particle and its diagonal particle in the voxel.
// The diagonal particle is retrieved using index relationships between different particles in the voxel (by construction).
float4 getVoxelCenterOfParticle(float4 particle, uint localParticleIdx) {
    int particleGlobalIdx = particleIndices[localParticleIdx];
    int voxelIdx = particleGlobalIdx >> 3;
    int particleIdxInVoxel = (particleGlobalIdx - (voxelIdx << 3));
    int particleDiagIdx = (voxelIdx << 3) + 7 - particleIdxInVoxel;
    float4 particleDiag = particles[particleDiagIdx];
    // It's okay to use the .w component from just one particle, because the particle radius will be the same for all particles in a voxel.
    return float4((particle.xyz + particleDiag.xyz) * 0.5f, particle.w); 
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
            float particleARadius = unpackHalf2x16(particleA.w).x;
            float particleBRadius = unpackHalf2x16(particleB.w).x;
            if (!doParticlesOverlap(particleA.xyz, particleB.xyz, particleARadius, particleBRadius, distanceSquared, particleAToB)) continue;
            particleAToB = normalize(particleAToB);
            float delta = (particleARadius + particleBRadius) - sqrt(distanceSquared);

            // Get the particles diagonal to A and B within their respective voxels.
            // Then approximate the voxel centers to augment collision normals, to avoid voxel interlock.
            float4 voxelACenter = getVoxelCenterOfParticle(particleA, particleStartIdx + i);
            float4 voxelBCenter = getVoxelCenterOfParticle(particleB, particleStartIdx + j);
            float voxelARadius = 1.5f * unpackHalf2x16(voxelACenter.w).x;
            float voxelBRadius = 1.5f * unpackHalf2x16(voxelBCenter.w).x;

            // Test for voxel-center collision, treating each center as an imaginary "particle" with radius 1.5x that of the voxel's real particles. 
            float3 augmentedNormal = particleAToB;
            float3 voxelAToB;
            if (doParticlesOverlap(voxelACenter, voxelBCenter, voxelARadius, voxelBRadius, distanceSquared, voxelAToB)) {
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