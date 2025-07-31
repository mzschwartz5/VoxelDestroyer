#include "particle_collisions_shared.hlsl"

StructuredBuffer<float4> particlePositions : register(t0);
StructuredBuffer<uint> isSurfaceVoxel : register(t1);
RWStructuredBuffer<uint> collisionCellParticleCounts : register(u0);
RWStructuredBuffer<uint> particlesByCollisionCell : register(u1);

[numthreads(BUILD_COLLISION_PARTICLE_THREADS, 1, 1)]
void main(uint3 gId : SV_DispatchThreadID)
{
    if (gId.x >= numParticles) return;

    int voxelIdx = gId.x >> 3;
    // Optimization: only compute collisions for surface particles by culling non-surface voxels early.
    if (!isSurfaceVoxel[voxelIdx]) {
        return;
    }

    float4 position = particlePositions[gId.x];
    int3 gridMinOverlap = int3(floor((position.xyz - particleRadius) * inverseCellSize));
    int3 gridMaxOverlap = int3(floor((position.xyz + particleRadius) * inverseCellSize));

    // Decrement the particle count for all cells that the particle overlaps.
    // Because we make the cells as large as the largest particle, this will be at most 8 cells.
    for (int z = gridMinOverlap.z; z <= gridMaxOverlap.z; ++z) {
        for (int y = gridMinOverlap.y; y <= gridMaxOverlap.y; ++y) {
            for (int x = gridMinOverlap.x; x <= gridMaxOverlap.x; ++x) {
                int cellHash = getParticleCellHash(x, y, z);
                int sortedParticleIndex;
                InterlockedAdd(collisionCellParticleCounts[cellHash], -1, sortedParticleIndex);
                particlesByCollisionCell[sortedParticleIndex] = gId.x;
            }
        }
    }


}