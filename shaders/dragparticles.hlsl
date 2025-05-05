RWStructuredBuffer<float4> particles : register(u0);
StructuredBuffer<float4> oldParticles : register(t0);
Texture2D<float> depthBuffer : register(t1);

cbuffer DragValues : register(b0)
{
    int mouseX;
    int mouseY;
    int dragX;
    int dragY;
    float selectRadius;
    float padding;
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
    float depthValue = depthBuffer.Load(int3(mouseX, mouseY, 0));

    // Calculate the voxel's center from the average position of the 8 voxel particles
    uint start_idx = gId.x << 3;
    float4 p0 = oldParticles[start_idx];
    float4 p1 = oldParticles[start_idx + 1];
    float4 p2 = oldParticles[start_idx + 2];
    float4 p3 = oldParticles[start_idx + 3];
    float4 p4 = oldParticles[start_idx + 4];
    float4 p5 = oldParticles[start_idx + 5];
    float4 p6 = oldParticles[start_idx + 6];
    float4 p7 = oldParticles[start_idx + 7];

    float4 voxelCenter = p0 + p1 + p2 + p3 + p4 + p5 + p6 + p7;
    voxelCenter *= 0.125f;
    voxelCenter.w = 1.0f;

    float4 pixelSpaceVoxelCenter = mul(voxelCenter, viewProj);
    pixelSpaceVoxelCenter /= pixelSpaceVoxelCenter.w; // Perspective divide
    pixelSpaceVoxelCenter.x = (pixelSpaceVoxelCenter.x + 1.0f) * 0.5f * viewportWidth;
    pixelSpaceVoxelCenter.y = (pixelSpaceVoxelCenter.y + 1.0f) * 0.5f * viewportHeight;

    // Compare the voxel center's depth to the scene depth value. If voxel is visible, move it.
    if (depthValue < pixelSpaceVoxelCenter.z) return;

    // Also compare the distance from the mouse to the voxel center
    float2 mousePos = float2(mouseX, mouseY);
    float2 voxelPos = float2(pixelSpaceVoxelCenter.x, pixelSpaceVoxelCenter.y);
    float2 diff = mousePos - voxelPos;
    float dist = length(diff);

    if (dist > selectRadius) return; 

    // OK: the voxel is visible and within the selection radius. Move its particles by the drag amount in world space.
    // To do this, we'll reverse-project the drag vector to world space, and then apply it to each particle.
    float4 drag = float4(
        depthValue * 2.0f * ((mouseX + dragX) / viewportWidth) - 1.0f, 
        depthValue * 2.0f * ((mouseY + dragY) / viewportHeight) - 1.0f, 
        depthValue, 
        1.0f
    );
    
    float4 dragWorld = mul(drag, invViewProj);

    float4 mouseStart = float4(
        depthValue * 2.0f * ((mouseX) / viewportWidth) - 1.0f,
        depthValue * 2.0f * ((mouseY) / viewportHeight) - 1.0f,
        depthValue,
        1.0f
    );

    float4 mouseStartWorld = mul(mouseStart, invViewProj);

    float4 dragWorldDiff = dragWorld - mouseStartWorld;
    dragWorldDiff.w = 0.0f; // Ignore the w component

    particles[start_idx] = p0 + dragWorldDiff;
    particles[start_idx + 1] = p1 + dragWorldDiff;
    particles[start_idx + 2] = p2 + dragWorldDiff;
    particles[start_idx + 3] = p3 + dragWorldDiff;
    particles[start_idx + 4] = p4 + dragWorldDiff;
    particles[start_idx + 5] = p5 + dragWorldDiff;
    particles[start_idx + 6] = p6 + dragWorldDiff;
    particles[start_idx + 7] = p7 + dragWorldDiff;
}