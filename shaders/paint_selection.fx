// This shader is used with the VoxelPaintRenderOperation to paint voxels faces or vertices (in progress) under the brush.
// It works in two passes: 
// 1. The first pass renders voxel instance IDs under the brush to an offscreen target. (As well as to per-voxel buffers).
// 2. The second pass uses that target to paint the voxels, debouncing voxels that were recently painted.

// Docs:
// Maya semantics: https://help.autodesk.com/view/MAYAUL/2022/ENU/?guid=Maya_SDK_Viewport_2_0_API_Shader_semantics_supported_by_html

struct VSIn  { float3 pos : POSITION; };
struct VSOut { float4 pos : SV_POSITION; nointerpolation uint globalVoxelID : INSTANCEID; };

float2 PAINT_POSITION;
float PAINT_RADIUS;
float PAINT_VALUE;
int PAINT_MODE; // 0 = subtract, 1 = set, 2 = add
float4x4 viewProjection : ViewProjection; // Maya-defined semantic, populated by Maya.

// VS-only resources
StructuredBuffer<float4x4> instanceTransforms : register(t0);
StructuredBuffer<uint> instanceToGlobalVoxelIdMap : register(t1);

// PS-only resources
// A bitmask of voxels painted this pass
RWStructuredBuffer<uint> paintedVoxelIDs : register(u1); // UAV registers live in the same namespace as outputs, must start at u1.
StructuredBuffer<uint> previousPaintedVoxelIDs : register(t2);
Buffer<float> previousVoxelPaintValue : register(t3);
RWBuffer<float> voxelPaintValue : register(u2); // NOT structured so that we can store half-precision floats.
Texture2D<uint> idRenderTarget : register(t4);

VSOut VS_Main(VSIn i, uint instanceID : SV_InstanceID) {
    VSOut o;
    float4x4 instanceTransform = instanceTransforms[instanceID];
    o.pos = mul(float4(i.pos, 1.0f), instanceTransform);
    o.pos = mul(o.pos, viewProjection);
    o.globalVoxelID = instanceToGlobalVoxelIdMap[instanceID];
    return o;
}

// The pass renders the IDs to an offscreen target. It's run with a scissor rect matching the brush area,
// then further culled to a circle in the pixel shader. Importantly, because of depth testing, only the nearest
// voxel under the brush will write its ID to each pixel.
// Note: this pass does not need to be run when doing non-camera-based painting.
uint PS_IDPass(VSOut psInput) : SV_Target {
    float2 delta = psInput.pos.xy - PAINT_POSITION;
    if (dot(delta, delta) > PAINT_RADIUS * PAINT_RADIUS) {
        return 0;
    }

    // 0 reserved for "no hit" (same as clear color)
    // Note: this means when using the selection results, we need to subtract 1 to get the original instance ID
    return psInput.globalVoxelID + 1;
}

float applyPaint(uint globalVoxelID, float prevPaintValue) {
    paintedVoxelIDs[globalVoxelID] = 1;
    // Do not paint voxels that have been painted recently (brush has to move away and then back to repaint)
    if (previousPaintedVoxelIDs[globalVoxelID] == 1) return prevPaintValue;
    
    // This branch has no chance of divergence, so there's no real penalty for it.
    int mode = PAINT_MODE - 1; // remap to -1,0,1
    if (mode == 0) {
        prevPaintValue = PAINT_VALUE;
    }
    else {
        prevPaintValue = prevPaintValue + mode * PAINT_VALUE;
    }
    
    prevPaintValue = clamp(prevPaintValue, 0.0f, 1.0f);
    voxelPaintValue[globalVoxelID] = prevPaintValue;
    return prevPaintValue;
}

// Updates paint values based on ID pass, and also renders all paint values to screen
float4 PS_PaintPass_CameraBased(VSOut psInput) : SV_Target {
    uint2 pixel = uint2(psInput.pos.xy);
    uint topVoxelId = idRenderTarget.Load(int3(pixel, 0));
    topVoxelId = topVoxelId - 1; // -1 to undo +1 in ID pass (careful of underflow)
    uint globalVoxelID = psInput.globalVoxelID;
    float prevPaintValue = previousVoxelPaintValue[globalVoxelID];

    // Only pixels in the brush area, and nearest the camera, will have their topVoxelId match their globalVoxelID.
    // (Note that, for pixels outside the brush, topVoxelId will be 0u - 1 --> UINT_MAX, so this condition will be false, as expected)    
    if (topVoxelId == globalVoxelID) {
        prevPaintValue = applyPaint(globalVoxelID, prevPaintValue);
    }

    return float4(1.0f, 0.0f, 0.0f, prevPaintValue);
}

float4 PS_PaintPass(VSOut psInput) : SV_Target {
    uint2 pixel = uint2(psInput.pos.xy);
    uint globalVoxelID = psInput.globalVoxelID;
    float prevPaintValue = previousVoxelPaintValue[globalVoxelID];
    float2 delta = psInput.pos.xy - PAINT_POSITION;

    // Only pixels in the brush area (whether occluded by other voxels on not) get painted.
    if (dot(delta, delta) <= PAINT_RADIUS * PAINT_RADIUS) {
        prevPaintValue = applyPaint(globalVoxelID, prevPaintValue);
    }

    return float4(1.0f, 0.0f, 0.0f, prevPaintValue);
}

// Simple render pass for when paint mode is active but the user is not actively painting
// We still need to render the existing paint values of the voxels, we just don't need to ID or update them.
float4 PS_RenderPass(VSOut psInput) : SV_Target {
    return float4(1.0f, 0.0f, 0.0f, voxelPaintValue[psInput.globalVoxelID]);
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