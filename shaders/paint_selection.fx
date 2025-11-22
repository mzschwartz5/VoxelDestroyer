// This shader is used with the VoxelPaintRenderOperation to paint voxels faces or vertices under the brush.
// It works in two passes: 
// 1. The first pass renders voxel instance IDs under the brush to an offscreen target. (As well as to per-voxel buffers).
// 2. The second pass uses that target to paint the voxels, debouncing voxels that were recently painted.

// Docs:
// Maya semantics: https://help.autodesk.com/view/MAYAUL/2022/ENU/?guid=Maya_SDK_Viewport_2_0_API_Shader_semantics_supported_by_html

struct VSIn  { 
    float3 pos : POSITION; 
    uint instanceID : SV_InstanceID;
    uint vertexID : SV_VertexID;
};

struct VSOut { 
    float4 pos : SV_POSITION; 
    nointerpolation uint globalVoxelID : INSTANCEID; 
#ifdef VERTEX_MODE
    nointerpolation float2 quadCenter : TEXCOORD0; // In screen space
#endif
};

float2 PAINT_POSITION;
float PAINT_RADIUS;
float PAINT_VALUE;
int PAINT_MODE; // 0 = subtract, 1 = set, 2 = add
float4 LOW_COLOR;
float4 HIGH_COLOR;
int COMPONENT_MASK; // Bitmask specifying which cardinal directions to paint (1 bit per direction, +X,+Y,+Z,-X,-Y,-Z)
float POINT_SIZE;   // The size of a voxel corner when rendering in vertex mode. (The size of the quad drawn at each corner).
float4x4 viewProjection : ViewProjection; // Maya-defined semantic, populated by Maya.
float2 viewportSize : ViewportPixelSize;  // Maya-defined semantic, populated by Maya.

#ifdef VERTEX_MODE
int COMPONENTS_PER_INSTANCE = 8; // 8 corners per voxel
#else
int COMPONENTS_PER_INSTANCE = 6; // 6 faces per voxel
#endif

// VS-only resources
StructuredBuffer<float4x4> instanceTransforms : register(t0);
StructuredBuffer<uint> instanceToGlobalVoxelIdMap : register(t1);

// PS-only resources
// A bitmask of voxels painted this pass
RWBuffer<uint> paintedVoxelIDs : register(u1);        // UAV registers live in the same namespace as outputs, must start at u1.
Buffer<uint> previousPaintedVoxelIDs : register(t2);
Buffer<float> previousVoxelPaintValue : register(t3); // NOT structured so that we can store half-precision floats.
RWBuffer<float> voxelPaintValue : register(u2);       // NOT structured so that we can store half-precision floats.
Texture2D<uint> idRenderTarget : register(t4);

VSOut VS_Main(VSIn vsIn) {
    VSOut vsOut;
    float4x4 instanceTransform = instanceTransforms[vsIn.instanceID];
#ifdef VERTEX_MODE
    // We can assume the voxel scale is uniform across all axes.
    float cubeScale = length(float3(instanceTransform[0][0], instanceTransform[1][0], instanceTransform[2][0]));
    vsOut.pos = expandVerticesToCorners(vsIn.vertexID, cubeScale);
#endif
    vsOut.pos = mul(float4(vsIn.pos, 1.0f), instanceTransform);
    vsOut.pos = mul(vsOut.pos, viewProjection);
#ifdef VERTEX_MODE
    vsOut.quadCenter = ((vsOut.pos.xy / vsOut.pos.w) + 1.0f) * 0.5f * viewportSize;
    vsOut.pos = expandCornerToQuad(vsIn.vertexID, vsOut.pos);
#endif
    vsOut.globalVoxelID = instanceToGlobalVoxelIdMap[vsIn.instanceID];
    return vsOut;
}

// In vertex mode, we draw a quad per voxel corner, with all 4 vertices starting at the origin.
// This step expands each group of 4 vertices out to the correct corner position.
float4 expandVerticesToCorners(uint vertexID, float cubeScale) {
    // Which cube corner (0..7) is this vertex part of (4 verts per corner so right shift by 2)
    uint cornerIdx = vertexID >> 2;

    // Extract bits into a float3
    float3 cornerBits = float3(
        float(cornerIdx & 1u),
        float((cornerIdx >> 1) & 1u),
        float((cornerIdx >> 2) & 1u)
    );

    // Map to {-0.5, +0.5}
    float3 cornerOffset = cornerBits - 0.5f; 
    // Scale to cube size (all verts start at origin)
    return float4(cornerOffset * cubeScale, 1.0f);
}

// In vertex mode, we draw a quad per voxel corner, with all 4 vertices starting at the origin.
// This step expands each vertex - already at the corner position, and projected to clip space - is expanded to a quad corner.
float4 expandCornerToQuad(uint vertexID, float4 cornerPosClip) {
    // Low 2 bits (0..3) specify which quad vertex this is
    uint quadVertex = vertexID & 3u;
    float2 quadBits = float2(
        float(quadVertex & 1u),            // x sign bit
        float((quadVertex >> 1) & 1u)      // y sign bit
    );

    // Map to {-0.5, +0.5}
    float2 quadOffset = quadBits - 0.5f;
    // Scale to NDC space (multiply by 2 because NDC goes from -1 to +1)
    float2 quadOffsetNDC = ((quadOffset * POINT_SIZE) / viewportSize) * 2.0f; 
    
    float4 expandedPos = cornerPosClip;
    expandedPos.xy += expandedPos.w * quadOffsetNDC; // Scale by w to stay correct after perspective divide
    return expandedPos;
}

// In vertex mode, we render quads, and cull pixels outside the point size radius
bool pixelOutsideRadius(float2 pixelCoord, float2 circleCenter, float radius) {
    float2 delta = pixelCoord - circleCenter; 
    if (dot(delta, delta) > radius * radius) {
        return true;
    }
    return false;
}

uint packVoxelIDs(uint voxelID, uint componentID) {
    return (voxelID << 3) | (componentID & 7u);
}
void unpackVoxelIDs(uint packed, out uint voxelID, out uint componentID) {
    componentID = packed & 7u;
    voxelID = (packed >> 3);
}

bool isComponentEnabled(uint componentID) {
    return (COMPONENT_MASK & (1 << componentID)) != 0;
}

// The pass renders the IDs to an offscreen target. It's run with a scissor rect matching the brush area,
// then further culled to a circle in the pixel shader. Importantly, because of depth testing, only the nearest
// voxel under the brush will write its ID to each pixel.
// Note: this pass does not need to be run when doing non-camera-based painting.
uint PS_IDPass(VSOut psInput, uint primID : SV_PrimitiveID) : SV_Target {
    if (pixelOutsideRadius(psInput.pos.xy, PAINT_POSITION, PAINT_RADIUS)) return 0;

    uint componentId = primID >> 1; // 2 triangles per face (and also two triangles per vertex, since they're quads)
    if (!isComponentEnabled(componentId)) return 0;

    // Add 1 to globalVoxelID so that 0 can represent "no voxel"
    return packVoxelIDs(psInput.globalVoxelID + 1, componentId);
}

float applyPaint(uint paintIdx, float prevPaintValue) {
    paintedVoxelIDs[paintIdx] = 1;
    // Do not paint voxels that have been painted recently (brush has to move away and then back to repaint)
    if (previousPaintedVoxelIDs[paintIdx] == 1) return prevPaintValue;

    // This branch has no chance of divergence, so there's no real penalty for it.
    int mode = PAINT_MODE - 1; // remap to -1,0,1
    // The "infinite strength" paint value is -1.0, and is only allowed in SET mode. 
    // In other modes, clamp the value to 0 before adding/subtracting to protect against infinite strength values. (Essentially means infinite values are treated as 0 in these modes).
    prevPaintValue = (mode == 0) ? PAINT_VALUE : saturate(max(prevPaintValue, 0) + mode * PAINT_VALUE);

    voxelPaintValue[paintIdx] = prevPaintValue;
    return prevPaintValue;
}

float4 colorFromPaintValue(float paintValue) {
    // Invert the higher value color if the paint value is infinite strength (sentinel value < 0)
    if (paintValue < 0.0f) {
        float3 invRGB = 1.0f - max(LOW_COLOR.rgb, HIGH_COLOR.rgb);
        float alpha = max(LOW_COLOR.a, HIGH_COLOR.a);
        return float4(invRGB, alpha);
    }

    return lerp(LOW_COLOR, HIGH_COLOR, paintValue);
}

// Updates paint values based on ID pass, and also renders all paint values to screen
float4 PS_PaintPass_CameraBased(VSOut psInput, uint primID : SV_PrimitiveID) : SV_Target {
#ifdef VERTEX_MODE
    // Return transparent pixel rather than discard to preserve early-z optimizations.
    if (pixelOutsideRadius(psInput.pos.xy, psInput.quadCenter, POINT_SIZE)) return float4(0,0,0,0); 
#endif

    uint2 pixel = uint2(psInput.pos.xy);
    uint topIDs = idRenderTarget.Load(int3(pixel, 0));
    uint topVoxelId, topComponentId;
    unpackVoxelIDs(topIDs, topVoxelId, topComponentId);
    topVoxelId -= 1; // Remap back to real voxel ID (...careful of underflow)

    uint globalVoxelID = psInput.globalVoxelID;
    uint componentId = primID >> 1; // 2 triangles per face (and also two triangles per vertex, since they're quads)
    uint idx = globalVoxelID * COMPONENTS_PER_INSTANCE + componentId;
    float prevPaintValue = previousVoxelPaintValue[idx];

    // Only pixels in the brush area, and nearest the camera, will have their topIDs match the current voxel.
    // (Note that, because an ID of 0 meant "no voxel", subtracting 1 above means voxel IDs outside the brush will underflow and not match any real voxel ID).
    if (topVoxelId == globalVoxelID && topComponentId == componentId) {
        prevPaintValue = applyPaint(idx, prevPaintValue);
    }

    return colorFromPaintValue(prevPaintValue);
}

float4 PS_PaintPass(VSOut psInput, uint primID : SV_PrimitiveID) : SV_Target {
#ifdef VERTEX_MODE
    // Return transparent pixel rather than discard to preserve early-z optimizations.
    if (pixelOutsideRadius(psInput.pos.xy, psInput.quadCenter, POINT_SIZE)) return float4(0,0,0,0); 
#endif

    uint2 pixel = uint2(psInput.pos.xy);
    uint globalVoxelID = psInput.globalVoxelID;
    uint componentId = primID >> 1; // 2 triangles per face (and also two triangles per vertex, since they're quads)
    bool componentEnabled = isComponentEnabled(componentId);
    uint idx = globalVoxelID * COMPONENTS_PER_INSTANCE + componentId;
    float prevPaintValue = previousVoxelPaintValue[idx];

    // Only pixels in the brush area (whether occluded by other voxels on not) get painted.
    if (componentEnabled && !pixelOutsideRadius(psInput.pos.xy, PAINT_POSITION, PAINT_RADIUS)) {
        prevPaintValue = applyPaint(idx, prevPaintValue);
    }

    return colorFromPaintValue(prevPaintValue);
}

// Simple render pass for when paint mode is active but the user is not actively painting
// We still need to render the existing paint values of the voxels, we just don't need to ID or update them.
float4 PS_RenderPass(VSOut psInput, uint primID : SV_PrimitiveID) : SV_Target {
#ifdef VERTEX_MODE
    // Return transparent pixel rather than discard to preserve early-z optimizations.
    if (pixelOutsideRadius(psInput.pos.xy, psInput.quadCenter, POINT_SIZE)) return float4(0,0,0,0); 
#endif

    uint globalVoxelID = psInput.globalVoxelID;
    uint componentId = primID >> 1; // 2 triangles per face (and also two triangles per vertex, since they're quads)
    uint idx = globalVoxelID * COMPONENTS_PER_INSTANCE + componentId;
    return colorFromPaintValue(voxelPaintValue[idx]);
}

technique11 PAINT_SELECTION_TECHNIQUE_NAME {
    pass IDPass{
        SetVertexShader( CompileShader(vs_5_0, VS_Main()) );
        SetPixelShader(  CompileShader(ps_5_0, PS_IDPass()) );
    }
    pass PaintPass_CameraBased {
        SetVertexShader( CompileShader(vs_5_0, VS_Main()) );
        SetPixelShader(  CompileShader(ps_5_0, PS_PaintPass_CameraBased()) );
    }
    pass PaintPass {
        SetVertexShader( CompileShader(vs_5_0, VS_Main()) );
        SetPixelShader(  CompileShader(ps_5_0, PS_PaintPass()) );
    }
    pass RenderPass {
        SetVertexShader( CompileShader(vs_5_0, VS_Main()) );
        SetPixelShader(  CompileShader(ps_5_0, PS_RenderPass()) );
    }
}