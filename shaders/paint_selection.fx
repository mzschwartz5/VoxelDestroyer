// Used with paint selection render operation to write selected voxels / faces + vertices to an offscreen render target.
StructuredBuffer<float4x4> InstanceTransforms : register(t0);
float4x4 ViewProjection : ViewProjection;

struct VSIn  { float3 pos : POSITION; };
struct VSOut { float4 pos : SV_POSITION; uint instanceID : INSTANCEID; };

float2 PAINT_POSITION;
float PAINT_RADIUS;

VSOut VSMain(VSIn i, uint instanceID : SV_InstanceID) {
    VSOut o;
    float4x4 instanceTransform = InstanceTransforms[instanceID];
    o.pos = mul(float4(i.pos, 1.0f), instanceTransform);
    o.pos = mul(o.pos, ViewProjection);
    // 0 reserved for "no hit"
    // Note: this means when reading back the selection results, we need to subtract 1 to get the original instance ID
    o.instanceID = instanceID + 1; 
    return o;
}

uint4 PSMain(VSOut i) : SV_Target {
    float2 delta = i.pos.xy - PAINT_POSITION;
    if (dot(delta, delta) > PAINT_RADIUS * PAINT_RADIUS) {
        // Return clear color rather than use discard (which disables early-z optimizations)
        // (Clear color is 0 so we can identify unselected pixels easily)
        return uint4(0, 0, 0, 1);
    }

    return uint4(i.instanceID, 0, 0, 1);
}

technique11 PAINT_SELECTION_TECHNIQUE_NAME {
    pass P0 {
        SetVertexShader( CompileShader(vs_5_0, VSMain()) );
        SetPixelShader(  CompileShader(ps_5_0, PSMain()) );
    }
}