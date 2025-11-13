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
float4x4 viewProjection : ViewProjection; // Maya-defined semantic, populated by Maya.

// VS-only resources
StructuredBuffer<float4x4> instanceTransforms : register(t0);
StructuredBuffer<uint> visibleToGlobalVoxelIdMap : register(t1);

// PS-only resources
// A bitmask of voxels painted this pass
// // TODO: re-enable once we have non-camera-based painting
// RWStructuredBuffer<uint> paintedVoxelIDs : register(u0);

VSOut VS_IDPass(VSIn i, uint instanceID : SV_InstanceID) {
    VSOut o;
    float4x4 instanceTransform = instanceTransforms[instanceID];
    o.pos = mul(float4(i.pos, 1.0f), instanceTransform);
    o.pos = mul(o.pos, viewProjection);
    o.globalVoxelID = visibleToGlobalVoxelIdMap[instanceID];
    return o;
}

uint4 PS_IDPass(VSOut psInput) : SV_Target {
    float2 delta = psInput.pos.xy - PAINT_POSITION;
    if (dot(delta, delta) > PAINT_RADIUS * PAINT_RADIUS) {
        // Return clear color rather than use discard (which disables early-z optimizations)
        // (Clear color is 0 so we can identify unselected pixels easily)
        // (Note that this doesn't prevent the depth from being written outside the brush, but that's acceptable)
        return uint4(0, 0, 0, 1);
    }

    // // TODO: only if camera-based painting is disabled
    uint globalVoxelID = psInput.globalVoxelID;
    // paintedVoxelIDs[globalVoxelID] = 1;

    // 0 reserved for "no hit"
    // Note: this means when using the selection results, we need to subtract 1 to get the original instance ID
    return uint4(globalVoxelID - 1, 0, 0, 1);
}

technique11 PAINT_SELECTION_TECHNIQUE_NAME {
    pass IDPass{
        SetVertexShader( CompileShader(vs_5_0, VS_IDPass()) );
        SetPixelShader(  CompileShader(ps_5_0, PS_IDPass()) );
    }
}