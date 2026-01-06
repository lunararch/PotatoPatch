Texture2D<float4> Frame0 : register(t0);
Texture2D<float4> Frame1 : register(t1);
Texture2D<float2> MotionVectors : register(t2);
RWTexture2D<float4> InterpolatedFrame : register(u0);

cbuffer Constants : register(b0)
{
    uint2 dimensions;
    float t; // Interpolation factor (0.5 for middle frame)
    uint blockSize;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pos = dispatchThreadID.xy;

    if (pos.x >= dimensions.x || pos.y >= dimensions.y)
        return;

    // Get motion vector for this block
    uint2 blockPos = pos / blockSize;
    float2 motion = MotionVectors[blockPos];

    // Calculate sample positions
    float2 samplePos0 = float2(pos) - motion * t;
    float2 samplePos1 = float2(pos) + motion * (1.0f - t);

    // Clamp to valid range
    samplePos0 = clamp(samplePos0, float2(0, 0), float2(dimensions) - 1.0f);
    samplePos1 = clamp(samplePos1, float2(0, 0), float2(dimensions) - 1.0f);

    // Sample both frames
    float4 color0 = Frame0[uint2(samplePos0)];
    float4 color1 = Frame1[uint2(samplePos1)];

    // Blend based on interpolation factor
    float4 result = lerp(color0, color1, t);

    InterpolatedFrame[pos] = result;
}