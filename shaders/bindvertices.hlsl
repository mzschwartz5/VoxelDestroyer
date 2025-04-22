StructuredBuffer<float4> particles : register(t0); 
StructuredBuffer<float> vertices : register(t1);
StructuredBuffer<uint> vertStartIds : register(t2);
StructuredBuffer<uint> numVerts : register(t3);
RWStructuredBuffer<float4> localRestPositions : register(u0);

groupshared float4 v0;

/* Each workgroup represents a voxel. Each thread in the workgroup handles 
 * (numVerts / BIND_VERTICES_THREADS) vertices from the voxel and calculates the local rest position for each.
*/
[numthreads(BIND_VERTICES_THREADS, 1, 1)]
void main(
    uint3 globalThreadId : SV_DispatchThreadID,
    uint3 groupId : SV_GroupID,
    uint3 localThreadId : SV_GroupThreadID
) {
    uint v0_idx = groupId.x << 3; // index of the minimum corner of the voxel
    if (localThreadId.x == 0) {
        v0 = particles[v0_idx];
    }
    GroupMemoryBarrierWithGroupSync();

    uint vertexStartIdx = vertStartIds[groupId.x];
    uint numVertsInVoxel = numVerts[groupId.x];
    uint numVertsPerThread = (numVertsInVoxel + BIND_VERTICES_THREADS - 1) / BIND_VERTICES_THREADS;

    for (uint i = 0; i < numVertsPerThread; i++) {
        uint vertexIdx = vertexStartIdx + localThreadId.x + (i * BIND_VERTICES_THREADS);
        if (vertexIdx >= vertexStartIdx + numVertsInVoxel) {
            continue;
        }

        float4 vertex = float4(vertices[3 * vertexIdx], vertices[3 * vertexIdx + 1], vertices[3 * vertexIdx + 2], 1.0f);
        localRestPositions[vertexIdx] = vertex - v0;
    }
}