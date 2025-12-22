#include "constants.hlsli"
#include "common.hlsl"

RWStructuredBuffer<float4> particles : register(u0);
RWStructuredBuffer<bool> isDragging : register(u1);
Texture2D<float> depthBuffer : register(t0);

cbuffer DragValues : register(b0)
{
    float3 dragWorldDiff;
    int lastMouseX;
    int lastMouseY;
    float selectRadius;
    float viewportWidth;
    float viewportHeight;
    float4x4 viewMatrix;
    float4x4 projMatrix;
};

/*
* This shader moves the mesh's particles according to the position of the user's mouse while dragging it. 
* It runs one-thread-per-voxel. Since voxels can't break apart, this is more efficient that running one-thread-per-particle, though slightly less precise.
*/
[numthreads(VGS_THREADS, 1, 1)]
void main( uint3 gId : SV_DispatchThreadID )
{
    // Sample the depth buffer at a specific location
    float depthValue = depthBuffer.Load(int3(lastMouseX, viewportHeight - lastMouseY, 0));

    // Calculate the voxel's center from the average position of the 8 voxel particles
    uint start_idx = gId.x << 3;
    float4 p0 = particles[start_idx + 0];
    float4 p1 = particles[start_idx + 1];
    float4 p2 = particles[start_idx + 2];
    float4 p3 = particles[start_idx + 3];
    float4 p4 = particles[start_idx + 4];
    float4 p5 = particles[start_idx + 5];
    float4 p6 = particles[start_idx + 6];
    float4 p7 = particles[start_idx + 7];

    float voxelSize = length(p0 - p7);
    float4 voxelCenter = float4(p0.xyz + p1.xyz + p2.xyz + p3.xyz + p4.xyz + p5.xyz + p6.xyz + p7.xyz, 1.0f);
    voxelCenter *= 0.125f;
    voxelCenter.w = 1.0f;

    float4 viewSpaceVoxelCenter = mul(viewMatrix, voxelCenter);
    viewSpaceVoxelCenter.z += (voxelSize * 0.5f);        // Bias the voxel towards the camera so the depth test later is really measuring the surface of the voxel.
    float voxelCameraDepth = -viewSpaceVoxelCenter.z;    // Later, we need to have the view-space depth of the voxel.
    
    // The voxel is behind or very near the camera, don't drag it.
    if (voxelCameraDepth < eps) {
        isDragging[gId.x] = false; 
        return;
    }

    float4 pixelSpaceVoxelCenter = mul(projMatrix,viewSpaceVoxelCenter);
    pixelSpaceVoxelCenter /= pixelSpaceVoxelCenter.w; // Perspective divide
    pixelSpaceVoxelCenter.x = (pixelSpaceVoxelCenter.x + 1.0f) * 0.5f * viewportWidth;
    pixelSpaceVoxelCenter.y = (pixelSpaceVoxelCenter.y + 1.0f) * 0.5f * viewportHeight;

    // Compare the voxel center's depth to the scene depth value. If voxel is visible, move it.
    if (depthValue < pixelSpaceVoxelCenter.z) {
        isDragging[gId.x] = false;
        return;
    }

    // Also compare the distance from the mouse to the voxel center
    float2 lastMousePos = float2(lastMouseX, lastMouseY);
    float2 diff = lastMousePos - float2(pixelSpaceVoxelCenter.x, pixelSpaceVoxelCenter.y);
    float dist = length(diff);

    // We want to take into account the size of the voxel when determining if it's within the selection radius.
    // Approximate its size by the average of two diagonal particles, and then account for perspective.
    float perspectiveVoxelSize = (0.5 * voxelSize / voxelCameraDepth) * max(viewportHeight, viewportWidth);

    if (dist > selectRadius + perspectiveVoxelSize) {
        isDragging[gId.x] = false;
        return; 
    }

    isDragging[gId.x] = true;

    // OK: the voxel is visible and within the selection radius. Move its particles by the drag amount in world space.
    // To do this, we'll reverse-project the mouse start/end points to world space, get the difference, and apply it to each particle.
    // For performance, the reverse-projection happens on the CPU to an imaginary voxel at unit-distance, then we just scale it here by the voxel's depth.
    // It looks unintuitive but it's just the mathematical result of some commutativity. Intuitively, it works because similar triangles.
    float3 scaledDragWorldDiff = voxelCameraDepth * pixelSpaceVoxelCenter.z * dragWorldDiff;

    if (!massIsInfinite(p0)) particles[start_idx + 0] = float4(p0.xyz + scaledDragWorldDiff, p0.w);
    if (!massIsInfinite(p1)) particles[start_idx + 1] = float4(p1.xyz + scaledDragWorldDiff, p1.w);
    if (!massIsInfinite(p2)) particles[start_idx + 2] = float4(p2.xyz + scaledDragWorldDiff, p2.w);
    if (!massIsInfinite(p3)) particles[start_idx + 3] = float4(p3.xyz + scaledDragWorldDiff, p3.w);
    if (!massIsInfinite(p4)) particles[start_idx + 4] = float4(p4.xyz + scaledDragWorldDiff, p4.w);
    if (!massIsInfinite(p5)) particles[start_idx + 5] = float4(p5.xyz + scaledDragWorldDiff, p5.w);
    if (!massIsInfinite(p6)) particles[start_idx + 6] = float4(p6.xyz + scaledDragWorldDiff, p6.w);
    if (!massIsInfinite(p7)) particles[start_idx + 7] = float4(p7.xyz + scaledDragWorldDiff, p7.w);
}