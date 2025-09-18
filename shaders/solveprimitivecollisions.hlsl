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

void solveSphereCollision(float4x4 wMatrix, inout float4 pos)
{
    float sphereRadius = wMatrix[0][0]; 
    float3 sphereCenter = float3(
        wMatrix[0][3],
        wMatrix[1][3],
        wMatrix[2][3]
    );

    float3 toParticle = pos.xyz - sphereCenter;
    float distSq = dot(toParticle, toParticle);
    float particleRadius = unpackHalf2x16(pos.w).x;
    float combinedRadiusSq = (sphereRadius + particleRadius) * (sphereRadius + particleRadius);
    if (distSq >= combinedRadiusSq) return; // Include particle radius

    float3 norm = toParticle / sqrt(distSq);
    pos.xyz = sphereCenter + norm * (sphereRadius + particleRadius);
}

void solveBoxCollision(float4x4 wMatrix, inout float4 pos)
{

}

void solvePlaneCollision(float4x4 wMatrix, inout float4 pos)
{
    float width = wMatrix[0][0];
    float height = wMatrix[1][1];
    float isInfinite = wMatrix[2][2];

    // Reset scale to 1 after extracting geometry info
    wMatrix[0][0] = 1.0f;
    wMatrix[1][1] = 1.0f;
    wMatrix[2][2] = 1.0f;
    wMatrix[3][3] = 1.0f;

    float3 planePos = mul(wMatrix, float4(0.0, 0.0, 0.0, 1.0)).xyz; 
    float3 planeNormal  = normalize(mul(wMatrix, float4(0.0, 1.0, 0.0, 0.0)).xyz); // plane normal
    float particleRadius = unpackHalf2x16(pos.w).x;
    float dist = dot(pos.xyz - planePos, planeNormal) - particleRadius;

    if (dist >= 0) return; // No collision

    // If the plane is finite, check bounds before resolving
    if (isInfinite == 0.0f) // 0 => finite, non-zero => infinite
    {
        float halfW = width * 0.5f;
        float halfH = height * 0.5f;

        // Plane local axes (right and forward)
        float3 planeRight = normalize(mul(wMatrix, float4(1.0, 0.0, 0.0, 0.0)).xyz);
        float3 planeForward = normalize(mul(wMatrix, float4(0.0, 0.0, 1.0, 0.0)).xyz);

        float3 toPoint = pos.xyz - planePos;
        float localX = dot(toPoint, planeRight);
        float localZ = dot(toPoint, planeForward);

        // If outside rectangle bounds, skip collision
        if (abs(localX) > halfW || abs(localZ) > halfH) return;
    }

    pos.xyz -= dist * planeNormal;
}

[numthreads(VGS_THREADS, 1, 1)]
void main(uint3 globalThreadId : SV_DispatchThreadID)
{
    if (numColliders == 0) return;
    if (globalThreadId.x >= (uint)totalParticles) return;
    
    float4 pos = particlePositions[globalThreadId.x];
    if (massIsInfinite(pos)) return;

    for (int i = 0; i < numColliders; i++) {
        float4x4 wMatrix = worldMatrix[i];
        int type = wMatrix[3][3];
        if (type == 0.0f) {
            solveBoxCollision(wMatrix, pos);
        } 
        else if (type == 1.0f) {
            solveSphereCollision(wMatrix, pos);
        }
        else if (type == 2.0f) {
            // Capsule collision logic goes here
        }
        else if (type == 3.0f) {
            // Cylinder collision logic goes here
        }
        else if (type == 4.0f) {
            solvePlaneCollision(wMatrix, pos);
        }
    }

    particlePositions[globalThreadId.x] = pos;
}