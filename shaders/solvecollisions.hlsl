// TODO: currently, this assumes equal particles weights! Account for different weights.
StructuredBuffer<int> collisionVoxelCounts : register(t0);
StructuredBuffer<int> collisionVoxelIndices : register(t1);
RWStructuredBuffer<float4> particlePositions : register(u0);

cbuffer ConstantBuffer : register(b0) {
    float voxelSize;
    float particleRadius;
    int numCollisionCells;
    float padding;
};

// This is a conservative estimate of overlap. Rather than accounting for the rotation of each voxel, and checking
// plane-plane intersections, we'll just check if the centers are within a voxel-diagonal distance of each other.
inline bool doVoxelsOverlap(float3 voxelACenter, float3 voxelBCenter) {
    // (Factor of 3 comes from the diagonal of a cube being sqrt(3) * edgeLength, and we're checking squared distances)
    return dot(voxelACenter - voxelBCenter, voxelACenter - voxelBCenter) < (3 * voxelSize * voxelSize);
}

inline bool doParticlesOverlap(float3 particleA, float3 particleB) {
    return dot(particleA - particleB, particleA - particleB) + 1e-6 < (4 * particleRadius * particleRadius);
}

/*
 * This shader is responsible for resolving collisions between particles. It uses the uniform grid acceleration structure
 * to find pairs of overlapping voxels. For each pair, it looks for pairs of overlapping particles and separates them along 
 * their collision normal. 
 *
 * Each thread does the work for one collision grid cell. 
*/
[numthreads(SOLVE_COLLISION_THREADS, 1, 1)]
void main(uint3 gId : SV_DispatchThreadID) {
    if (gId.x >= numCollisionCells) return;

    // Start by loading all the voxels in this collision cell. Since each will be referenced multiple times, this will help performance.
    // No need for shared memory here, as this optimization is thread-level (per collision cell), not group level.
    int voxels[MAX_VOXELS_PER_CELL];
    int voxelCount = collisionVoxelCounts[gId.x];
    
    for (int v = 0; v < voxelCount; ++v) {
        int voxelIndex = collisionVoxelIndices[gId.x * MAX_VOXELS_PER_CELL + v];
        if (voxelIndex == -1) break; // No more voxels in this cell.

        voxels[v] = voxelIndex;
    }

    // Note: This could cause register pressure issues. It's *probably* okay though.
    // If it truly becomes a problem, consider shared memory or maybe chunking these loads.
    float3 particlesA[8];
    float3 particlesB[8];
    const float epsilon = 1e-6f;
    // Now check for overlap between pairs of voxels
    for (int i = 0; i < voxelCount; ++i) {
        for (int j = i + 1; j < voxelCount; ++j) {
            // Get voxel center by averaging two diagonal particles of the voxel.
            // Because we morton-ordered the particles, v_diag(j) = v0 + 7 - j
            float4 p0_A = particlePositions[voxels[i] << 3];
            float4 p7_A = particlePositions[(voxels[i] << 3) + 7];
            float4 p0_B = particlePositions[voxels[j] << 3];
            float4 p7_B = particlePositions[(voxels[j] << 3) + 7];

            float3 voxelACenter = (p0_A.xyz + p7_A.xyz) * 0.5f;
            float3 voxelBCenter = (p0_B.xyz + p7_B.xyz) * 0.5f;

            if (!doVoxelsOverlap(voxelACenter, voxelBCenter)) continue;

            // If they overlap, now we need to check for overlap between particles in the two voxels, and resolve any collisions there.
            // First, load in the particles in the two voxels, again, as a performance optimization to avoid repeated global reads.
            particlesA[0] = p0_A.xyz - (particleRadius * sign(p0_A.xyz - voxelACenter)); // The input buffer is really the positions of voxel corners; the actual particles are inside.
            particlesA[7] = p7_A.xyz - (particleRadius * sign(p7_A.xyz - voxelACenter));
            particlesB[0] = p0_B.xyz - (particleRadius * sign(p0_B.xyz - voxelBCenter));
            particlesB[7] = p7_B.xyz - (particleRadius * sign(p7_B.xyz - voxelBCenter));

            for (int k = 1; k < 7; ++k) {
                float3 pk_A = particlePositions[(voxels[i] << 3) + k].xyz;
                float3 pk_B = particlePositions[(voxels[j] << 3) + k].xyz;
                particlesA[k] = pk_A - (particleRadius * sign(pk_A - voxelACenter));
                particlesB[k] = pk_B - (particleRadius * sign(pk_B - voxelBCenter));
            }

            for (int r = 0; r < 8; ++r) {
                for (int s = 0; s < 8; ++s) {
                    if (!doParticlesOverlap(particlesA[r], particlesB[s])) continue;

                    // Resolve collision
                    float3 particleVector = particlesA[r] - particlesB[s];
                    float particleDistance = length(particleVector);
                    float3 collisionNormal = particleVector / max(particleDistance, epsilon);
                    float offsetDistance = 2.0f * particleRadius - particleDistance;

                    // Move particles apart along the collision normal
                    // TODO: use an "imaginary" center particle to augment collision normals to avoid voxel interlock
                    particlesA[r] += collisionNormal * (offsetDistance);
                    particlesB[s] -= collisionNormal * (offsetDistance);
                }
            }

            // Write the updated particles back to the buffer
            for (int t = 0; t < 8; ++t) {
                particlePositions[(voxels[i] << 3) + t] = float4(particlesA[t] + (particleRadius * sign(particlesA[t] - voxelACenter)), 1.0f);
                particlePositions[(voxels[j] << 3) + t] = float4(particlesB[t] + (particleRadius * sign(particlesB[t] - voxelBCenter)), 1.0f);
            }
        }
    }
}