#define TIMESTEP 0.00166666666666667f // (60FPS with 10 substeps)

StructuredBuffer<float> weights : register(t0);
StructuredBuffer<float4> positions : register(t1);
StructuredBuffer<float4> oldPositions : register(t2);
RWStructuredBuffer<float4> velocities : register(u0);
RWStructuredBuffer<bool> isDragging : register(u1);

[numthreads(VGS_THREADS, 1, 1)]
void main(uint3 gId : SV_DispatchThreadID) 
{
    // Check for out of bounds?

    if (weights[gId.x] == 0.0f) {
        return;
    }

    // While dragging a voxel, don't update its velocity, or else it can shoot off when released.
    int voxelIndex = gId.x >> 3;
    if (isDragging[voxelIndex]) {
        velocities[gId.x] = float4(0, 0, 0, 0);
        isDragging[voxelIndex] = false; // Reset for next frame; the drag shader must re-set this to true every frame.
        return;
    }

    velocities[gId.x] = (positions[gId.x] - oldPositions[gId.x]) / TIMESTEP;
}