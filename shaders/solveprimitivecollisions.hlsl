#include "common.hlsl"

RWStructuredBuffer<float4> particlePositions : register(u0);

/**
 * Store colliders in a constant buffer for fast access. We assume a relatively small number of colliders.
 */
cbuffer ColliderBuffer : register(b0)
{    
    float4x4 worldMatrix[MAX_COLLIDERS];
    int totalParticles;
    int numColliders;
    int padding[2];
};

void solveSphereCollision(int colliderIndex, inout float4 pos)
{
    float sphereRadius = worldMatrix[colliderIndex][0][0]; 
    float3 sphereCenter = float3(
        worldMatrix[colliderIndex][0][3],
        worldMatrix[colliderIndex][1][3],
        worldMatrix[colliderIndex][2][3]
    );

    float3 toParticle = pos.xyz - sphereCenter;
    float distSq = dot(toParticle, toParticle);
    float particleRadius = unpackHalf2x16(pos.w).x;
    float combinedRadiusSq = (sphereRadius + particleRadius) * (sphereRadius + particleRadius);
    if (distSq >= combinedRadiusSq) return; // Include particle radius

    float3 norm = toParticle / sqrt(distSq);
    pos.xyz = sphereCenter + norm * (sphereRadius + particleRadius);
}

[numthreads(VGS_THREADS, 1, 1)]
void main(uint3 globalThreadId : SV_DispatchThreadID)
{
    if (numColliders == 0) return;
    if (globalThreadId.x >= (uint)totalParticles) return;
    
    float4 pos = particlePositions[globalThreadId.x];
    if (massIsInfinite(pos)) return;

    for (int i = 0; i < numColliders; i++) {
        int type = worldMatrix[i][3][3];
        if (type == 0.0f) {
            // Box collision logic goes here
        } 
        else if (type == 1.0f) {
            solveSphereCollision(i, pos);
        }
        else if (type == 2.0f) {
            // Capsule collision logic goes here
        }
        else if (type == 3.0f) {
            // Cylinder collision logic goes here
        }
        else if (type == 4.0f) {
            // Plane collision logic goes here
        }
    }

    particlePositions[globalThreadId.x] = pos;
}