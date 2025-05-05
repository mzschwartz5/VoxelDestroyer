RWStructuredBuffer<float4> particles : register(u0);
Texture2D<float> depthBuffer : register(t0);
SamplerState depthSampler : register(s0);

[numthreads(1, 1, 1)]
void main( uint3 dispatchThreadID : SV_DispatchThreadID )
{
    // Sample the depth buffer at a specific location
    float2 uv = float2(dispatchThreadID.x, dispatchThreadID.y) / float2(1920, 1080); // Assuming a 1920x1080 resolution
    float depthValue = depthBuffer.Sample(depthSampler, uv).r;
}