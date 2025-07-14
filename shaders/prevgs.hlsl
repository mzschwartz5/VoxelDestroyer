
StructuredBuffer<float> weights : register(t0);
RWStructuredBuffer<float4> positions : register(u0);
RWStructuredBuffer<float4> oldPositions : register(u1);
RWStructuredBuffer<float4> velocities : register(u2); 
RWStructuredBuffer<bool> isDragging : register(u3);

cbuffer VoxelSimBuffer : register(b0)
{
    float GRAVITY_STRENGTH;
    float GROUND_ENABLED;
    float GROUND_Y;
    float TIMESTEP;
};

[numthreads(VGS_THREADS, 1, 1)]
void main(uint3 gId : SV_DispatchThreadID) 
{
    // TODOs:
    // Check for out of bounds?
    // Consider accumulating or taking a max velocity while dragging and then applying it after the drag ends.
    
    if (weights[gId.x] == 0.0f) return;

    velocities[gId.x] = (positions[gId.x] - oldPositions[gId.x]) / TIMESTEP;
    oldPositions[gId.x] = positions[gId.x];
    
    int voxelIndex = gId.x >> 3;
    if (!isDragging[voxelIndex]) {
        velocities[gId.x] += float4(0, GRAVITY_STRENGTH, 0, 0) * TIMESTEP; // Gravity
        positions[gId.x].xyz += (velocities[gId.x] * TIMESTEP).xyz; // Update position
    }

    // For now, lump ground collision into this shader
    if (GROUND_ENABLED == 1.f && positions[gId.x].y < GROUND_Y) {
        positions[gId.x] = oldPositions[gId.x];
        positions[gId.x].y = GROUND_Y;
    }
}