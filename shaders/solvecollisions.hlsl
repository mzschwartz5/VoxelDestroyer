#include "particle_collisions_shared.hlsl"

StructuredBuffer<uint> particleIndices : register(t0);
StructuredBuffer<uint> collisionCellParticleCounts : register(t1);
RWStructuredBuffer<float4> particles : register(u0);

// Max out shared memory. See note below about how many particles each thread can store, based
// on the number of threads per workgroup and how many threads are assigned to each cell. (And see constants.h for SOLVE_COLLISION_THREADS).
groupshared float4 s_particles[2048];

/**
 * Resolve collisions between particles in the same collision cell. (Particles have been pre-binned into all cells they overlap)
 * 
 * This compute pass is a bit tricky in terms of mapping threads / workgroups to work items. We could have one thread to one grid cell, but this has two issues: 
 *    1. It can churn on outlier cells that have above average particle counts, and 
 *    2. With a reasonable number of threads in a workgroup, each thread can only put a small number of particles into the limited shared memory 
 *       (for 256 threads per workgroup, each thread (cell) would only be able to store 8 particles in the 32KB shared memory per workgroup).
 * 
 * We could map a whole workgroup to a cell, but:
 *    1. Many particles contain very few, if any, particles - by design! A workgroup is overkill in these cases.
 *    2. With a typical hash table size equal to the number of particles (common for uniform hash grids), this would be an unwieldy number of workgroups to dispatch many times per frame.
 *
 * The approach taken here is a compromise: small groups of threads (maybe 2-4) within a workgroup collaborate to process a single cell. This increases the number of particles per cell that can 
 * be stored in shared memory by a factor equal to the number of threads working on each cell. For example, for a 256 thread workgroup with 4 threads per cell, each cell can now store up to 32 particles in shared memory.
 * Moreover, outlier cells with many particles now get processed by multiple threads. (And in such a way that avoids divergent behavior within a warp).
*/
[numthreads(SOLVE_COLLISION_THREADS, 1, 1)]
void main(uint3 globalId : SV_DispatchThreadID, uint3 groupThreadId : SV_GroupThreadID) {
    uint collisionCellIdx = (uint)(globalId.x * THREADS_PER_COLLISION_CELL_INV);
    if (collisionCellIdx >= hashGridSize) return;

    uint particleStartIdx = collisionCellParticleCounts[collisionCellIdx];
    uint particleEndIdx = collisionCellParticleCounts[collisionCellIdx + 1]; // No out of bounds concern because we added a guard (extra buffer entry) for this very purpose.

    uint collisionCellIdxInGroup = (uint)(groupThreadId.x * THREADS_PER_COLLISION_CELL_INV);
    uint sharedMemoryStartIdx = particleStartIdx - collisionCellParticleCounts[collisionCellIdx - collisionCellIdxInGroup];

    // Store particles in shared memory.
    uint numParticlesInCell = particleEndIdx - particleStartIdx;
    uint particlesPerThread = (uint)(numParticlesInCell * THREADS_PER_COLLISION_CELL_INV);
    uint threadIdxInCell = globalId.x - collisionCellIdx * THREADS_PER_COLLISION_CELL;

    for (uint i = 0; i < particlesPerThread; ++i) {
        s_particles[sharedMemoryStartIdx + (threadIdxInCell * particlesPerThread) + i] =
            particles[particleIndices[particleStartIdx + (threadIdxInCell * particlesPerThread) + i]];
    }
    GroupMemoryBarrierWithGroupSync();

    // Now we can finally resolve collisions in shared memory, dividing up the work among the threads collaborating on this cell.
}