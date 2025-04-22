StructuredBuffer<float4> particles : register(t0); 
StructuredBuffer<uint> vertStartIds : register(t1);
StructuredBuffer<uint> numVerts : register(t2);
StructuredBuffer<float4> localRestPositions : register(t3);
RWStructuredBuffer<float4> transformedPositions : register(u0); 

// Voxel minimum corner
groupshared float4 v0;

// New basis vectors of the voxel
groupshared float4 e0;
groupshared float4 e1;
groupshared float4 e2;

/*
* Each workgroup represents a voxel. Each thread in the workgroup handles 
* (numVerts / BIND_VERTICES_THREADS) vertices from the voxel and transforms them based on the deformed voxel's basis.
*/
[numthreads(TRANSFORM_VERTICES_THREADS, 1, 1)]
void main(
    uint3 globalThreadId : SV_DispatchThreadID,
    uint3 groupId : SV_GroupID,
    uint3 localThreadId : SV_GroupThreadID
) {
    uint v0_idx = groupId.x << 3;
    if (localThreadId.x == 0) {
        v0 = particles[v0_idx];
        float4 v1 = particles[v0_idx + 1];
        float4 v2 = particles[v0_idx + 2];
        float4 v4 = particles[v0_idx + 4];

        e0 = v1 - v0;
        e1 = v2 - v0;
        e2 = v4 - v0;
    }
    GroupMemoryBarrierWithGroupSync();

    uint vertexStartIdx = vertStartIds[groupId.x];
    uint numVertsInVoxel = numVerts[groupId.x];
    uint numVertsPerThread = (numVertsInVoxel + TRANSFORM_VERTICES_THREADS - 1) / TRANSFORM_VERTICES_THREADS;

    for (uint i = 0; i < numVertsPerThread; i++) {
        uint vertexIdx = vertexStartIdx + localThreadId.x + (i * TRANSFORM_VERTICES_THREADS);
        if (vertexIdx >= vertexStartIdx + numVertsInVoxel) {
            continue;
        }

        float4 localRestPosition = localRestPositions[vertexIdx];
        float4 transformedPos = v0 + (localRestPosition.x * e0) + (localRestPosition.y * e1) + (localRestPosition.z * e2);
        transformedPositions[vertexIdx] = float4(transformedPos.x, transformedPos.y, transformedPos.z, 1.0f);
    }
}