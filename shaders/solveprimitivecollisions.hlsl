#include "common.hlsl"

RWStructuredBuffer<float4> particlePositions : register(u0);

/**
 * Store colliders in a constant buffer for fast access. We assume a relatively small number of colliders.
 */
cbuffer ColliderBuffer : register(b0)
{    
    float4x4 worldMatrix[MAX_COLLIDERS];
    float4x4 inverseWorldMatrix[MAX_COLLIDERS];
    int totalParticles;
    int numColliders;
    int padding[2];
};

void resetMatrixScale(inout float4x4 wMatrix)
{
    wMatrix[0][0] = 1.0f;
    wMatrix[1][1] = 1.0f;
    wMatrix[2][2] = 1.0f;
    wMatrix[3][3] = 1.0f;
}

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

void solveBoxCollision(float4x4 wMatrix, float4x4 invWMatrix, inout float4 pos)
{
    // In the box's object space, the box is axis-aligned and easier to work with
    float4 localPos = mul(invWMatrix, float4(pos.xyz, 1.0f));
    float3 halfExtents = float3(wMatrix[0][0] * 0.5f, wMatrix[1][1] * 0.5f, wMatrix[2][2] * 0.5f);
    resetMatrixScale(wMatrix);

    // Closest point on axis-aligned box in local space
    // If the particle center is inside the box, this will just return localPos, and delta will be zero
    float3 clamped = clamp(localPos.xyz, -halfExtents, halfExtents);
    float3 delta = localPos.xyz - clamped;
    float distSq = dot(delta, delta);

    float particleRadius = unpackHalf2x16(pos.w).x;
    float radiusSq = particleRadius * particleRadius;

    if (distSq >= radiusSq) return;

    float3 adjustedLocalPos = localPos;
    if (distSq > 0.0f) { // particle center outside box
        float invSqrtDist = rsqrt(distSq);
        float3 normalLocal = delta * invSqrtDist;
        adjustedLocalPos = clamped + normalLocal * particleRadius;
    } else { // particle center inside box: push out to nearest face
        float3 distsToFace = halfExtents - abs(localPos.xyz);
        
        // Get axis of nearest face
        int axis = 0;
        if (distsToFace.y < distsToFace.x) axis = 1;
        if (distsToFace.z < distsToFace[axis]) axis = 2;

        // Avoid dynamic vector indexing (can break compiler unrolling). Use explicit branches.
        if (axis == 0) {
            float axisSign = (localPos.x < 0.0f) ? -1.0f : 1.0f;
            adjustedLocalPos.x = (halfExtents.x + particleRadius) * axisSign;
        } else if (axis == 1) {
            float axisSign = (localPos.y < 0.0f) ? -1.0f : 1.0f;
            adjustedLocalPos.y = (halfExtents.y + particleRadius) * axisSign;
        } else {
            float axisSign = (localPos.z < 0.0f) ? -1.0f : 1.0f;
            adjustedLocalPos.z = (halfExtents.z + particleRadius) * axisSign;
        }
    }

    pos.xyz = mul(wMatrix, float4(adjustedLocalPos, 1.0f)).xyz;
}

void solvePlaneCollision(float4x4 wMatrix, inout float4 pos)
{
    float width = wMatrix[0][0];
    float height = wMatrix[1][1];
    float isInfinite = wMatrix[2][2];
    resetMatrixScale(wMatrix);

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
        float4x4 invWMatrix = inverseWorldMatrix[i];
        int type = wMatrix[3][3];
        if (type == 0.0f) {
            solveBoxCollision(wMatrix, invWMatrix, pos);
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