StructuredBuffer<float4> particlePositions : register(t0);
StructuredBuffer<uint> isSurfaceVoxel : register(t1);
RWStructuredBuffer<uint> collisionCellParticleCounts : register(u0);
RWStructuredBuffer<float4> particlePositionsByCollisionCell : register(u1);

[numthreads(BUILD_COLLISION_PARTICLES_THREADS, 1, 1)]
void main(uint3 gId : SV_DispatchThreadID)
{
    if (gId.x >= numParticles) return;

    int voxelIdx = gId.x >> 8;
    // Optimization: only compute collisions for surface particles by culling non-surface voxels early.
    if (!isSurfaceVoxel[voxelIdx]) {
        return;
    }

    float4 position = particlePositions[gId.x];
    int cellHash = getParticleCellHash(position.xyz);

    int sortedParticleIndex;
    InterlockedAdd(collisionCellParticleCounts[cellHash], -1, sortedParticleIndex);

    particlePositionsByCollisionCell[sortedParticleIndex] = position;
}