#include "particle_collisions_shared.hlsl"
#include "common.hlsl"
#include "constants.hlsli"

StructuredBuffer<uint> particleIndices : register(t0);
StructuredBuffer<uint> collisionCellParticleCounts : register(t1);
StructuredBuffer<Particle> frameStartParticles : register(t2);
RWStructuredBuffer<Particle> particles : register(u0);

static const float jitterEpsilon = 1e-3f;
static const float relaxationFactor = 0.35f;

bool doParticlesOverlap(
    float3 positionA,
    float3 positionB,
    float radiusA,
    float radiusB,
    out float distanceSquared, 
    out float3 particleAToB
)
{
    particleAToB = positionB - positionA;
    distanceSquared = dot(particleAToB, particleAToB);
    if (distanceSquared < 1e-6f) return false; // Avoid division by zero or NaN

    return (distanceSquared < (radiusA + radiusB) * (radiusA + radiusB));
}

// Approximates the center of a voxel that owns a particle by the average of the particle and its diagonal particle in the voxel.
// The diagonal particle is retrieved using index relationships between different particles in the voxel (by construction).
float3 getVoxelCenterOfParticle(Particle particle, uint particleGlobalIdx, uint voxelIdx) {
    int particleIdxInVoxel = (particleGlobalIdx - (voxelIdx << 3));
    int particleDiagIdx = (voxelIdx << 3) + 7 - particleIdxInVoxel;
    Particle particleDiag = particles[particleDiagIdx];
    return (particle.position + particleDiag.position) * 0.5f;
}

// Max out shared memory. See note below about how many particles each thread can store, based
// on the number of threads per workgroup and how many threads are assigned to each cell. (And see constants.hlsli for SOLVE_COLLISION_THREADS).
#define SHARED_MEMORY_SIZE 1365 // maximum number of (float4 + uint + bool)'s can fit in 32KB of shared memory
groupshared Particle s_particles[SHARED_MEMORY_SIZE];
groupshared uint s_globalParticleIndices[SHARED_MEMORY_SIZE];
groupshared bool s_positionChanged[SHARED_MEMORY_SIZE];

void applyFriction(Particle preCollisionA, Particle preCollisionB, Particle frameStartParticleA, Particle frameStartParticleB, inout Particle postCollisionA, inout Particle postCollisionB, float3 augmentedNormal, float collisionDelta) {
    if (friction <= 0) return;

    float3 frameDeltaA = preCollisionA.position - frameStartParticleA.position;
    float3 frameDeltaB = preCollisionB.position - frameStartParticleB.position;
    float3 relFrameDelta = frameDeltaA - frameDeltaB;
    float3 relTangent = relFrameDelta - dot(relFrameDelta, augmentedNormal) * augmentedNormal;

    float relTangentLenSq = dot(relTangent, relTangent);
    if (relTangentLenSq < 1e-8f) return;

    float maxTangent = friction * collisionDelta;
    float scale = min(1.0f, maxTangent / sqrt(relTangentLenSq));
    float3 relTangentClamped = relTangent * scale;

    float invMassA = particleInverseMass(preCollisionA);
    float invMassB = particleInverseMass(preCollisionB);
    float invMassSumReciprocal = 1 / (invMassA + invMassB);

    postCollisionA.position -= (invMassA * invMassSumReciprocal) * relTangentClamped;
    postCollisionB.position += (invMassB * invMassSumReciprocal) * relTangentClamped;
}

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
        if (sharedMemoryStartIdx + u >= SHARED_MEMORY_SIZE) break; // Ignore any particles that would overflow shared memory.
        uint globalParticleIdx = particleIndices[particleStartIdx + u];
        s_globalParticleIndices[sharedMemoryStartIdx + u] = globalParticleIdx;
        s_particles[sharedMemoryStartIdx + u] = particles[globalParticleIdx];
        s_positionChanged[sharedMemoryStartIdx + u] = false; // Initialize position changed flags.
    }

    for (uint i = 0; i < numParticlesInCell; ++i) {
        if (sharedMemoryStartIdx + i >= SHARED_MEMORY_SIZE) break;
        for (uint j = i + 1; j < numParticlesInCell; ++j) {
            if (sharedMemoryStartIdx + j >= SHARED_MEMORY_SIZE) break;

            uint sharedMemIdx_i = sharedMemoryStartIdx + i;
            uint sharedMemIdx_j = sharedMemoryStartIdx + j;
            
            uint globalParticleIdx_i = s_globalParticleIndices[sharedMemIdx_i];
            uint globalParticleIdx_j = s_globalParticleIndices[sharedMemIdx_j];
            
            uint globalVoxelIdx_i = globalParticleIdx_i >> 3;
            uint globalVoxelIdx_j = globalParticleIdx_j >> 3;
            
            if (globalVoxelIdx_i == globalVoxelIdx_j) continue; // Skip particle pairs from the same voxel.

            Particle particleA = s_particles[sharedMemIdx_i];
            Particle particleB = s_particles[sharedMemIdx_j];

            float distanceSquared;
            float3 particleAToB;
            float2 particleRadiusAndInvMassA = unpackHalf2x16(particleA.radiusAndInvMass);
            float2 particleRadiusAndInvMassB = unpackHalf2x16(particleB.radiusAndInvMass);
            float invMassSum = particleRadiusAndInvMassA.y + particleRadiusAndInvMassB.y;
            if (invMassSum <= 0.0f) continue; // Both particles are immovable.

            if (!doParticlesOverlap(particleA.position, particleB.position, particleRadiusAndInvMassA.x, particleRadiusAndInvMassB.x, distanceSquared, particleAToB)) continue;
            particleAToB = normalize(particleAToB);
            float delta = (particleRadiusAndInvMassA.x + particleRadiusAndInvMassB.x) - sqrt(distanceSquared);
            
            float jitterThreshold = jitterEpsilon * min(particleRadiusAndInvMassA.x, particleRadiusAndInvMassB.x);            
            if (delta <= jitterThreshold) continue;
            delta -= jitterThreshold; 

            // Get the particles diagonal to A and B within their respective voxels.
            // Then approximate the voxel centers to augment collision normals, to avoid voxel interlock.
            float3 voxelACenter = getVoxelCenterOfParticle(particleA, globalParticleIdx_i, globalVoxelIdx_i);
            float3 voxelBCenter = getVoxelCenterOfParticle(particleB, globalParticleIdx_j, globalVoxelIdx_j);

            // Test for voxel-center collision, treating each center as an imaginary "particle" with radius 1.5x that of the voxel's real particles. 
            float3 augmentedNormal = particleAToB;
            float3 voxelAToB;
            if (doParticlesOverlap(voxelACenter, voxelBCenter, 1.5f * particleRadiusAndInvMassA.x, 1.5f * particleRadiusAndInvMassB.x, distanceSquared, voxelAToB)) {
                augmentedNormal = normalize(normalize(voxelAToB) + particleAToB);
            }
            
            float invMassSumReciprocal = 1 / (invMassSum);

            s_particles[sharedMemIdx_i].position -= delta * relaxationFactor * (invMassSumReciprocal * particleRadiusAndInvMassA.y) * augmentedNormal;
            s_particles[sharedMemIdx_j].position += delta * relaxationFactor * (invMassSumReciprocal * particleRadiusAndInvMassB.y) * augmentedNormal;

            // We do have to do some extra per-collision-pair global reads to apply friction. There's not enough shared memory to store frame start positions.
            // But since these reads only happen on actual collisions (not on all candidates), it's manageable.
            applyFriction(
                particleA, 
                particleB,
                frameStartParticles[globalParticleIdx_i],
                frameStartParticles[globalParticleIdx_j],
                s_particles[sharedMemIdx_i],
                s_particles[sharedMemIdx_j],
                augmentedNormal,
                delta
            );

            s_positionChanged[sharedMemIdx_i] = true;
            s_positionChanged[sharedMemIdx_j] = true;
        }
    }

    // Write the particles back to global memory.
    for (uint v = 0; v < numParticlesInCell; ++v) {
        if (sharedMemoryStartIdx + v >= SHARED_MEMORY_SIZE) break; // Ignore any particles that would overflow shared memory.
        if (!s_positionChanged[sharedMemoryStartIdx + v]) continue;

        uint globalParticleIdx = s_globalParticleIndices[sharedMemoryStartIdx + v];
        particles[globalParticleIdx] = s_particles[sharedMemoryStartIdx + v];
    }
}