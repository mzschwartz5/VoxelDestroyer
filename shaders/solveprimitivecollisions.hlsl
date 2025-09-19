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
    wMatrix[3][0] = 0.0f;
    wMatrix[3][1] = 0.0f;
    wMatrix[3][2] = 0.0f;
    wMatrix[3][3] = 1.0f;
}

void solveSphereCollision(float4x4 wMatrix, inout float4 pos, float particleRadius)
{
    float sphereRadius = wMatrix[3][0]; 
    float3 sphereCenter = float3(
        wMatrix[0][3],
        wMatrix[1][3],
        wMatrix[2][3]
    );

    float3 toParticle = pos.xyz - sphereCenter;
    float distSq = dot(toParticle, toParticle);
    float combinedRadiusSq = (sphereRadius + particleRadius) * (sphereRadius + particleRadius);
    if (distSq >= combinedRadiusSq) return; // Include particle radius

    float3 norm = toParticle / sqrt(distSq);
    pos.xyz = sphereCenter + norm * (sphereRadius + particleRadius);
}

void solveBoxCollision(float4x4 wMatrix, float4x4 invWMatrix, inout float4 pos, float particleRadius)
{
    // In the box's object space, the box is axis-aligned and easier to work with
    float3 localPos = mul(invWMatrix, float4(pos.xyz, 1.0f)).xyz;
    float3 halfExtents = float3(wMatrix[3][0] * 0.5f, wMatrix[3][1] * 0.5f, wMatrix[3][2] * 0.5f);

    // Closest point on axis-aligned box in local space
    // If the particle center is inside the box, this will just return localPos, and delta will be zero
    float3 clamped = clamp(localPos, -halfExtents, halfExtents);
    float3 delta = localPos - clamped;
    float distSq = dot(delta, delta);

    float radiusSq = particleRadius * particleRadius;
    if (distSq >= radiusSq) return;

    float3 adjustedLocalPos = localPos;
    if (distSq > 0.0f) { // particle center outside box
        float invSqrtDist = rsqrt(distSq);
        float3 normalLocal = delta * invSqrtDist;
        adjustedLocalPos = clamped + normalLocal * particleRadius;
    } else { // particle center inside box: push out to nearest face
        float3 distsToFace = halfExtents - abs(localPos);
        
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

    resetMatrixScale(wMatrix);
    pos.xyz = mul(wMatrix, float4(adjustedLocalPos, 1.0f)).xyz;
}

void solvePlaneCollision(float4x4 wMatrix, inout float4 pos, float particleRadius)
{
    float width = wMatrix[3][0];
    float height = wMatrix[3][1];
    float isInfinite = wMatrix[3][2];
    resetMatrixScale(wMatrix);

    float3 planePos = mul(wMatrix, float4(0.0, 0.0, 0.0, 1.0)).xyz; 
    float3 planeNormal  = normalize(mul(wMatrix, float4(0.0, 1.0, 0.0, 0.0)).xyz); // plane normal
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

void solveCylinderCollision(float4x4 wMatrix, float4x4 invWMatrix, inout float4 pos, float particleRadius)
{
}

void solveCapsuleCollision(float4x4 wMatrix, float4x4 invWMatrix, inout float4 pos, float particleRadius)
{
    float capsuleRadius = wMatrix[3][0];
    float halfHeight = wMatrix[3][1] * 0.5f;

    // In the capsule's object space, the capsule is aligned with the Y axis and easier to work with
    float3 localPos = mul(invWMatrix, float4(pos.xyz, 1.0f)).xyz;

    // Capsule body endpoints (i.e. not including the caps)
    float3 a = float3(0.0f, -halfHeight, 0.0f);
    float3 b = float3(0.0f,  halfHeight, 0.0f);
    float3 ab = b - a;
    float abLenSq = dot(ab, ab);

    // Closest point on segment to particle center
    float t = dot(localPos - a, ab) / abLenSq;
    t = clamp(t, 0.0f, 1.0f);
    float3 closest = a + ab * t;

    float3 delta = localPos - closest;
    float distSq = dot(delta, delta);

    float combined = capsuleRadius + particleRadius;
    float combinedSq = combined * combined;

    if (distSq >= combinedSq) return;

    float3 adjustedLocalPos;
    if (distSq > 0.0f) {
        float invLen = rsqrt(distSq);
        float3 normalLocal = delta * invLen;
        adjustedLocalPos = closest + normalLocal * combined;
    } else {
        // Particle exactly on the capsule spine; push out along local X
        adjustedLocalPos = closest + float3(combined, 0.0f, 0.0f);
    }

    resetMatrixScale(wMatrix);
    pos.xyz = mul(wMatrix, float4(adjustedLocalPos, 1.0f)).xyz;
}

[numthreads(VGS_THREADS, 1, 1)]
void main(uint3 globalThreadId : SV_DispatchThreadID)
{
    if (numColliders == 0) return;
    if (globalThreadId.x >= (uint)totalParticles) return;
    
    float4 pos = particlePositions[globalThreadId.x];
    if (massIsInfinite(pos)) return;
    float particleRadius = unpackHalf2x16(pos.w).x;

    for (int i = 0; i < numColliders; i++) {
        float4x4 wMatrix = worldMatrix[i];
        float4x4 invWMatrix = inverseWorldMatrix[i];
        int type = wMatrix[3][3];
        if (type == 0.0f) {
            solveBoxCollision(wMatrix, invWMatrix, pos, particleRadius);
        } 
        else if (type == 1.0f) {
            solveSphereCollision(wMatrix, pos, particleRadius);
        }
        else if (type == 2.0f) {
            solveCapsuleCollision(wMatrix, invWMatrix, pos, particleRadius);
        }
        else if (type == 3.0f) {
            solveCylinderCollision(wMatrix, invWMatrix, pos, particleRadius);
        }
        else if (type == 4.0f) {
            solvePlaneCollision(wMatrix, pos, particleRadius);
        }
    }

    particlePositions[globalThreadId.x] = pos;
}