RWStructuredBuffer<float4> particles : register(u0);
RWStructuredBuffer<bool> isDragging : register(u1);
Texture2D<float> depthBuffer : register(t0);

static const float eps = 1e-8f;

cbuffer DragValues : register(b0)
{
    int lastMouseX;
    int lastMouseY;
    int currMouseX;
    int currMouseY;
    float selectRadius;
    float padding;
    float viewportWidth;
    float viewportHeight;
    float4x4 viewMatrix;
    float4x4 projMatrix;
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
    float depthValue = depthBuffer.Load(int3(lastMouseX, viewportHeight - lastMouseY, 0));

    // Calculate the voxel's center from the average position of the 8 voxel particles
    uint start_idx = gId.x << 3;
    float4 p0 = particles[start_idx];
    float4 p1 = particles[start_idx + 1];
    float4 p2 = particles[start_idx + 2];
    float4 p3 = particles[start_idx + 3];
    float4 p4 = particles[start_idx + 4];
    float4 p5 = particles[start_idx + 5];
    float4 p6 = particles[start_idx + 6];
    float4 p7 = particles[start_idx + 7];

    float voxelSize = length(p0 - p7);
    float4 voxelCenter = p0 + p1 + p2 + p3 + p4 + p5 + p6 + p7;
    voxelCenter *= 0.125f;
    voxelCenter.w = 1.0f;

    float4 viewSpaceVoxelCenter = mul(voxelCenter, viewMatrix);
    viewSpaceVoxelCenter.z += (voxelSize * 0.5f);        // Bias the voxel towards the camera so the depth test later is really measuring the surface of the voxel.
    float voxelCameraDepth = -viewSpaceVoxelCenter.z;    // Later, we need to have the view-space depth of the voxel.
    
    // The voxel is behind or very near the camera, don't drag it.
    if (voxelCameraDepth < eps) return;

    float4 pixelSpaceVoxelCenter = mul(viewSpaceVoxelCenter, projMatrix);
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
    // Approximate its size by the average of two diagonal particles, and then account for perspective.
    float perspectiveVoxelSize = (0.5 * voxelSize / voxelCameraDepth) * max(viewportHeight, viewportWidth);

    if (dist > selectRadius + perspectiveVoxelSize) return; 

    isDragging[gId.x] = true;

    // OK: the voxel is visible and within the selection radius. Move its particles by the drag amount in world space.
    // To do this, we'll reverse-project the mouse start/end points to world space, get the difference, and apply it to each particle.
    float2 mouseStartNDC = float2((lastMouseX / viewportWidth) * 2.0f - 1.0f,
                                  (lastMouseY / viewportHeight) * 2.0f - 1.0f);
    float4 mouseStartClip = voxelCameraDepth * float4(mouseStartNDC, pixelSpaceVoxelCenter.z, 1.0f);
    float4 mouseStartWorld = mul(mouseStartClip, invViewProj);

    float2 mouseEndNDC = float2((currMouseX / viewportWidth) * 2.0f - 1.0f,
                                (currMouseY / viewportHeight) * 2.0f - 1.0f);
    float4 mouseEndClip = voxelCameraDepth * float4(mouseEndNDC, pixelSpaceVoxelCenter.z, 1.0f);
    float4 mouseEndWorld = mul(mouseEndClip, invViewProj);

    float4 dragWorldDiff = mouseEndWorld - mouseStartWorld;
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