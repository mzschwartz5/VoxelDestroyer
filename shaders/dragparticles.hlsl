RWStructuredBuffer<float4> particles : register(u0);
StructuredBuffer<float4> oldParticles : register(t0);
Texture2D<float> depthBuffer : register(t1);

cbuffer DragValues : register(b0)
{
    int lastMouseX;
    int lastMouseY;
    int currMouseX;
    int currMouseY;
    float selectRadius;
    float dragStrength;
    float viewportWidth;
    float viewportHeight;
    float4x4 viewProj;
    float4x4 invViewProj;
};

/*
* This shader moves the mesh's particles according to the position of the user's mouse while dragging it. 
* It runs one-thread-per-voxel. Since voxels can't break apart, this is more efficient that running one-thread-per-particle, though slightly less precise.
*/
[numthreads(VGS_THREADS, 1, 1)]
void main( uint3 gId : SV_DispatchThreadID )
{
    // Sample the depth buffer at a specific location
    float depthValue = depthBuffer.Load(int3(lastMouseX, lastMouseY, 0));

    // Calculate the voxel's center from the average position of the 8 voxel particles
    uint start_idx = gId.x << 3;
    float4 oldPositions[8];
    float4 voxelCenter = float4(0.0f, 0.0f, 0.0f, 0.0f);
    [unroll]
    for (int i = 0; i < 8; ++i)
    {
        oldPositions[i] = oldParticles[start_idx + i];
        voxelCenter += oldPositions[i];
    }

    voxelCenter *= 0.125f;
    voxelCenter.w = 1.0f;

    float4 pixelSpaceVoxelCenter = mul(voxelCenter, viewProj);
    float voxelCameraDepth = pixelSpaceVoxelCenter.w; // Before perspective divide, this is the camera-space depth of the voxel center
    pixelSpaceVoxelCenter /= pixelSpaceVoxelCenter.w; // Perspective divide
    pixelSpaceVoxelCenter.x = (pixelSpaceVoxelCenter.x + 1.0f) * 0.5f * viewportWidth;
    pixelSpaceVoxelCenter.y = (pixelSpaceVoxelCenter.y + 1.0f) * 0.5f * viewportHeight;

    // Compare the voxel center's depth to the scene depth value. If voxel is visible, move it.
    if (depthValue < pixelSpaceVoxelCenter.z) return;

    // Also compare the distance from the mouse to the voxel center
    float2 lastMousePos = float2(lastMouseX, lastMouseY);
    float2 diff = lastMousePos - float2(pixelSpaceVoxelCenter.x, pixelSpaceVoxelCenter.y);
    float dist = length(diff);

    // We want to take into account the size of the voxel when determining if it's within the selection radius.
    // Approximate its size by the average of two diagonal particles, and then account for perspective since we're comparing in screen space.
    float voxelSize = length(oldPositions[0] - oldPositions[7]);
    float perspectiveVoxelSize = voxelSize / voxelCameraDepth;

    if (dist > selectRadius + perspectiveVoxelSize) return; 

    // OK: the voxel is visible and within the selection radius. Move its particles by dragging each in screen space,
    // then converting back to world space to write to the particles buffer.
    float2 currMousePos = float2(currMouseX, currMouseY);
    float2 screenDragAmount = currMousePos - lastMousePos;

    [unroll]
    for (int j = 0; j < 8; ++j)
    {
        float4 oldPos = oldPositions[j];
        float4 oldPosScreen = mul(oldPos, viewProj);
        float oldCameraDepth = oldPosScreen.w;
        oldPosScreen /= oldPosScreen.w;
        oldPosScreen.x = (oldPosScreen.x + 1.0f) * 0.5f * viewportWidth;
        oldPosScreen.y = (oldPosScreen.y + 1.0f) * 0.5f * viewportHeight;

        oldPosScreen.x += dragStrength * screenDragAmount.x;
        oldPosScreen.y += dragStrength * screenDragAmount.y;

        oldPosScreen.x = clamp(oldPosScreen.x, 0.0f, viewportWidth - 1.0f);
        oldPosScreen.y = clamp(oldPosScreen.y, 0.0f, viewportHeight - 1.0f);

        // Convert back to world space
        oldPosScreen.x = (oldPosScreen.x / viewportWidth) * 2.0f - 1.0f;
        oldPosScreen.y = (oldPosScreen.y / viewportHeight) * 2.0f - 1.0f;
        float4 newPosClip = oldCameraDepth * float4(oldPosScreen.xyz, 1.0f);
        float4 newPosWorld = mul(newPosClip, invViewProj);
        newPosWorld.w = 1.0f;

        particles[start_idx + j] = newPosWorld;
    }
}