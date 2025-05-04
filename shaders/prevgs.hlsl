
StructuredBuffer<float> weights : register(t0);
RWStructuredBuffer<float4> positions : register(u0);
RWStructuredBuffer<float4> oldPositions : register(u1);
RWStructuredBuffer<float4> velocities : register(u2); 

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
    // Check for out of bounds?
    
    if (weights[gId.x] == 0.0f) {
        return;
    }

    oldPositions[gId.x] = positions[gId.x];
    // TODO: it may be possible to do this velocity update at the end of the PBD step, and not have to bind it to this shader.
    velocities[gId.x] += float4(0, GRAVITY_STRENGTH, 0, 0) * TIMESTEP; // Gravity
    positions[gId.x] += velocities[gId.x] * TIMESTEP; // Update position

    // For now, lump ground collision into this shader
    if (GROUND_ENABLED == 1.f && positions[gId.x].y < GROUND_Y) {
        positions[gId.x] = oldPositions[gId.x];
        positions[gId.x].y = GROUND_Y;
    }
}