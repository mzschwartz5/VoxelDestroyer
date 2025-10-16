// Used with paint selection render operation to write selected voxels / faces + vertices to an offscreen render target.
StructuredBuffer<float4x4> InstanceTransforms : register(t0);
float4x4 ViewProjection : ViewProjection;

struct VSIn  { float3 pos : POSITION; };
struct VSOut { float4 pos : SV_POSITION; };

VSOut VSMain(VSIn i, uint instanceID : SV_InstanceID) {
    VSOut o;
    float4x4 instanceTransform = InstanceTransforms[instanceID];
    o.pos = mul(float4(i.pos, 1.0f), instanceTransform);
    o.pos = mul(o.pos, ViewProjection);
    return o;
}

uint4 PSMainUInt(VSOut i) : SV_Target {
    return uint4(1, 0, 0, 1);
}

technique11 PAINT_SELECTION_TECHNIQUE_NAME {
    pass P0 {
        SetVertexShader( CompileShader(vs_5_0, VSMain()) );
        SetPixelShader(  CompileShader(ps_5_0, PSMainUInt()) );
    }
}