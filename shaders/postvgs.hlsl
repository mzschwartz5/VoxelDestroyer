#define TIMESTEP 0.00166666666666667f // (60FPS with 10 substeps)

StructuredBuffer<float> weights : register(t0);
StructuredBuffer<float4> positions : register(t1);
StructuredBuffer<float4> oldPositions : register(t2);
RWStructuredBuffer<float4> velocities : register(u0);

[numthreads(VGS_THREADS, 1, 1)]
void main(uint3 gId : SV_DispatchThreadID) 
{
    // Check for out of bounds?

    if (weights[gId.x] == 0.0f) {
        return;
    }

    velocities[gId.x] = (positions[gId.x] - oldPositions[gId.x]) / TIMESTEP;
}