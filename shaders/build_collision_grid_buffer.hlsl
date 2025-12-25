#include "particle_collisions_shared.hlsl"
#include "common.hlsl"
#include "constants.hlsli"

StructuredBuffer<Particle> particles : register(t0);
StructuredBuffer<uint> isSurfaceVoxel : register(t1);
RWStructuredBuffer<uint> collisionCellParticleCounts : register(u0);

/**
 * In this shader, each thread represents a particle. We take a hash of the particle's grid position to bin it into a cell,
 * and (atomically) increment the count of particles in that cell.
 */
[numthreads(BUILD_COLLISION_GRID_THREADS, 1, 1)]
void main(uint3 gId : SV_DispatchThreadID)
{
    if (gId.x >= numParticles) return;

    int voxelIdx = gId.x >> 3;
    // Optimization: only compute collisions for surface particles by culling non-surface voxels early.
    if (!isSurfaceVoxel[voxelIdx]) {
        return;
    }

    Particle particle = particles[gId.x];
    float radius = particleRadius(particle);
    int3 gridMinOverlap = int3(floor((particle.position - radius) * inverseCellSize));
    int3 gridMaxOverlap = int3(floor((particle.position + radius) * inverseCellSize));

    // Increment the particle count for all cells that the particle overlaps.
    // Because we make the cells as large as the largest particle, this will be at most 8 cells.
    for (int z = gridMinOverlap.z; z <= gridMaxOverlap.z; ++z) {
        for (int y = gridMinOverlap.y; y <= gridMaxOverlap.y; ++y) {
            for (int x = gridMinOverlap.x; x <= gridMaxOverlap.x; ++x) {
                int cellHash = getParticleCellHash(x, y, z);
                InterlockedAdd(collisionCellParticleCounts[cellHash], 1);
            }
        }
    }
}