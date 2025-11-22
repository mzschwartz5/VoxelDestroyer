// This shader is used with the VoxelPaintRenderOperation to paint voxels faces or vertices (in progress) under the brush.
// It works in two passes: 
// 1. The first pass renders voxel instance IDs under the brush to an offscreen target. (As well as to per-voxel buffers).
// 2. The second pass uses that target to paint the voxels, debouncing voxels that were recently painted.

// Docs:
// Maya semantics: https://help.autodesk.com/view/MAYAUL/2022/ENU/?guid=Maya_SDK_Viewport_2_0_API_Shader_semantics_supported_by_html

struct VSIn  { float3 pos : POSITION; };
struct VSOut { 
    float4 pos : SV_POSITION; 
    nointerpolation uint globalVoxelID : INSTANCEID; 
};

float2 PAINT_POSITION;
float PAINT_RADIUS;
float PAINT_VALUE;
int PAINT_MODE; // 0 = subtract, 1 = set, 2 = add
float4 LOW_COLOR;
float4 HIGH_COLOR;
int COMPONENT_MASK; // Bitmask specifying which cardinal directions to paint (1 bit per direction, +X,+Y,+Z,-X,-Y,-Z)
float4x4 viewProjection : ViewProjection; // Maya-defined semantic, populated by Maya.
float INF_SENTINEL = -1.0f; // Sentinel value indicating infinite strength painting.

// VS-only resources
StructuredBuffer<float4x4> instanceTransforms : register(t0);
StructuredBuffer<uint> instanceToGlobalVoxelIdMap : register(t1);

// PS-only resources
// A bitmask of voxels painted this pass
RWStructuredBuffer<uint> paintedVoxelIDs : register(u1); // UAV registers live in the same namespace as outputs, must start at u1.
StructuredBuffer<uint> previousPaintedVoxelIDs : register(t2);
Buffer<float> previousVoxelPaintValue : register(t3); // NOT structured so that we can store half-precision floats.
RWBuffer<float> voxelPaintValue : register(u2);       // NOT structured so that we can store half-precision floats.
Texture2D<uint> idRenderTarget : register(t4);

VSOut VS_Main(VSIn i, uint instanceID : SV_InstanceID) {
    VSOut o;
    float4x4 instanceTransform = instanceTransforms[instanceID];
    o.pos = mul(float4(i.pos, 1.0f), instanceTransform);
    o.pos = mul(o.pos, viewProjection);
    o.globalVoxelID = instanceToGlobalVoxelIdMap[instanceID];
    return o;
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
    float2 delta = psInput.pos.xy - PAINT_POSITION;
    if (dot(delta, delta) > PAINT_RADIUS * PAINT_RADIUS) return 0;

    uint faceID = primID >> 1; // 2 triangles per face. TODO: generalize for vertex painting.
    if (!isComponentEnabled(faceID)) return 0;

    // Add 1 to globalVoxelID so that 0 can represent "no voxel"
    return packVoxelIDs(psInput.globalVoxelID + 1, faceID);
}

float applyPaint(uint globalVoxelID, uint componentID, float prevPaintValue) {
    uint idx = globalVoxelID * 6 + componentID; // TODO: generalize so we can paint vertices too
    paintedVoxelIDs[idx] = 1;
    // Do not paint voxels that have been painted recently (brush has to move away and then back to repaint)
    if (previousPaintedVoxelIDs[idx] == 1) return prevPaintValue;

    // This branch has no chance of divergence, so there's no real penalty for it.
    int mode = PAINT_MODE - 1; // remap to -1,0,1
    // The "infinite strength" sentinel value is -1.0, and only works in SET mode. 
    // In other modes, clamp the value to 0 before adding/subtracting to protect against infinite strength values. (Essentially means infinite values are treated as 0 in these modes).
    prevPaintValue = (mode == 0) ? PAINT_VALUE : saturate(max(prevPaintValue, 0) + mode * PAINT_VALUE);

    voxelPaintValue[idx] = prevPaintValue;
    return prevPaintValue;
}

float4 colorFromPaintValue(float paintValue) {
    // Invert the higher value color if the paint value is infinite strength
    if (paintValue == INF_SENTINEL) {
        float3 invRGB = 1.0f - max(LOW_COLOR.rgb, HIGH_COLOR.rgb);
        float alpha = max(LOW_COLOR.a, HIGH_COLOR.a);
        return float4(invRGB, alpha);
    }

    return lerp(LOW_COLOR, HIGH_COLOR, paintValue);
}

// Updates paint values based on ID pass, and also renders all paint values to screen
float4 PS_PaintPass_CameraBased(VSOut psInput, uint primID : SV_PrimitiveID) : SV_Target {
    uint2 pixel = uint2(psInput.pos.xy);
    uint topIDs = idRenderTarget.Load(int3(pixel, 0));
    uint topVoxelId, topComponentId;
    unpackVoxelIDs(topIDs, topVoxelId, topComponentId);
    topVoxelId -= 1; // Remap back to real voxel ID (...careful of underflow)

    uint globalVoxelID = psInput.globalVoxelID;
    uint faceID = primID >> 1; // 2 triangles per face. TODO: generalize for vertex painting.
    uint idx = globalVoxelID * 6 + faceID;
    float prevPaintValue = previousVoxelPaintValue[idx];

    // Only pixels in the brush area, and nearest the camera, will have their topIDs match the current voxel.
    // (Note that, because an ID of 0 meant "no voxel", subtracting 1 above means voxel IDs outside the brush will underflow and not match any real voxel ID).
    if (topVoxelId == globalVoxelID && topComponentId == faceID) {
        prevPaintValue = applyPaint(globalVoxelID, topComponentId, prevPaintValue);
    }

    return colorFromPaintValue(prevPaintValue);
}

float4 PS_PaintPass(VSOut psInput, uint primID : SV_PrimitiveID) : SV_Target {
    uint2 pixel = uint2(psInput.pos.xy);
    uint globalVoxelID = psInput.globalVoxelID;
    uint faceID = primID >> 1; // 2 triangles per face. TODO: generalize for vertex painting.
    bool componentEnabled = isComponentEnabled(faceID);
    uint idx = globalVoxelID * 6 + faceID;
    float prevPaintValue = previousVoxelPaintValue[idx];
    float2 delta = psInput.pos.xy - PAINT_POSITION;

    // Only pixels in the brush area (whether occluded by other voxels on not) get painted.
    if (componentEnabled && dot(delta, delta) <= PAINT_RADIUS * PAINT_RADIUS) {
        prevPaintValue = applyPaint(globalVoxelID, faceID, prevPaintValue);
    }

    return colorFromPaintValue(prevPaintValue);
}

// Simple render pass for when paint mode is active but the user is not actively painting
// We still need to render the existing paint values of the voxels, we just don't need to ID or update them.
float4 PS_RenderPass(VSOut psInput, uint primID : SV_PrimitiveID) : SV_Target {
    uint globalVoxelID = psInput.globalVoxelID;
    uint faceID = primID >> 1; // 2 triangles per face. TODO: generalize for vertex painting.
    uint idx = globalVoxelID * 6 + faceID;

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