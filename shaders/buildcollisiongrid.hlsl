StructuredBuffer<float4> particlePositions : register(t0);
StructuredBuffer<uint> isSurfaceVoxel : register(t1);
RWStructuredBuffer<int> collisionVoxelCounts : register(u0);
RWStructuredBuffer<int> collisionVoxelIndices : register(u1);

cbuffer ConstantBuffer : register(b0) {
    float3 gridMin;
    float voxelSize;
    int3 gridDims;
    float padding0;
    float3 gridInvCellDims; // inverse on CPU to avoid division in shader
    float padding1;
};

int3 voxelPosToGridCellIndex(float3 voxelPos) {
    int cellIdxX = floor((voxelPos.x - gridMin.x) * gridInvCellDims.x);
    int cellIdxY = floor((voxelPos.y - gridMin.y) * gridInvCellDims.y);
    int cellIdxZ = floor((voxelPos.z - gridMin.z) * gridInvCellDims.z);

    return int3(cellIdxX, cellIdxY, cellIdxZ);
}

inline int to1D(int3 index, int3 dimensions) {
    return index.x + (index.y * dimensions.x) + (index.z * dimensions.x * dimensions.y);
}

/**
 * In this shader, we populate the uniform collision volume grid for a voxelized mesh.
 * We only add surface voxels to the grid, as interior voxels cannot be collided with.
 * On breaking face constraints, interior voxels can be added to the grid.
 * Each thread represents a voxel. 
 * Note that the uniform grid approach assumes that a grid cell is bigger than a voxel.
 */
[numthreads(BUILD_COLLISION_THREADS, 1, 1)]
void main(uint3 gId : SV_DispatchThreadID) {
    if (!isSurfaceVoxel[gId.x]) {
        return;
    }
    
    // Estimate the voxel center by averaging two diagonal particles of the voxel.
    // Because we morton-ordered the particles, v_diag(j) = v0 + 7 - j
    int v0 = gId.x << 3;
    int v1 = v0 + 7;
    float3 voxelCenterPos = (particlePositions[v0].xyz + particlePositions[v1].xyz) * 0.5f;
    int3 cellIdx3D = voxelPosToGridCellIndex(voxelCenterPos);

    // Determine which octant of the grid cell the voxel center is in.
    // Then we can use that to find which neighboring grid cells it might overlap.
    float halfGridDimsMinusOne = (gridDims - 1) * 0.5f;
    int3 voxelOctantSign = int3(trunc((cellIdx3D - halfGridDimsMinusOne) / halfGridDimsMinusOne)); // Each (x, y, z) ∈ {0, 1, -1}
    int3 neighborIdx3d = clamp(voxelPosToGridCellIndex(voxelCenterPos + voxelOctantSign * voxelSize), 
                               int3(0, 0, 0), 
                               gridDims - 1
                              );
    int3 minIterBounds = min(cellIdx3D, neighborIdx3d);
    int3 maxIterBounds = max(cellIdx3D, neighborIdx3d);

    // This will iterate over all cells the voxel could overlap, including this thread's cell.
    // It can include up to 8 cells, but in practice will be 1 the vast majority of the time.
    for (int z = minIterBounds.z; z <= maxIterBounds.z; ++z) {
        for (int y = minIterBounds.y; y <= maxIterBounds.y; ++y) {
            for (int x = minIterBounds.x; x <= maxIterBounds.x; ++x) {
                int3 neighborCellIdx = int3(x, y, z);
                int neighborCellIdx1D = to1D(neighborCellIdx, gridDims);

                int voxelIndexInCell;
                InterlockedAdd(collisionVoxelCounts[neighborCellIdx1D], 1, voxelIndexInCell);
                if (voxelIndexInCell < MAX_VOXELS_PER_CELL) {
                    collisionVoxelIndices[neighborCellIdx1D * MAX_VOXELS_PER_CELL + voxelIndexInCell] = gId.x;
                }
            }
        }
    }
}