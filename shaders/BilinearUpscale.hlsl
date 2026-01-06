#include "Common.hlsli"

Texture2D<float4> InputTexture : register(t0);
RWTexture2D<float4> OutputTexture : register(u0);

cbuffer Constants : register(b0)
{
    float2 inputSize;
    float2 outputSize;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 outputPos = dispatchThreadID.xy;

    if (outputPos.x >= outputSize.x || outputPos.y >= outputSize.y)
        return;

    // Calculate normalized coordinates
    float2 uv = (float2(outputPos) + 0.5f) / outputSize;

    // Sample input texture with bilinear filtering
    float2 inputUV = uv * inputSize;

    // Manual bilinear interpolation
    float2 texelPos = inputUV - 0.5f;
    float2 f = frac(texelPos);
    int2 texelInt = int2(floor(texelPos));

    // Clamp to valid range
    texelInt = clamp(texelInt, int2(0, 0), int2(inputSize.x - 1, inputSize.y - 1));

    // Sample 4 neighboring pixels
    float4 s00 = InputTexture[texelInt + int2(0, 0)];
    float4 s10 = InputTexture[min(texelInt + int2(1, 0), int2(inputSize.x - 1, inputSize.y - 1))];
    float4 s01 = InputTexture[min(texelInt + int2(0, 1), int2(inputSize.x - 1, inputSize.y - 1))];
    float4 s11 = InputTexture[min(texelInt + int2(1, 1), int2(inputSize.x - 1, inputSize.y - 1))];

    // Bilinear interpolation
    float4 s0 = lerp(s00, s10, f.x);
    float4 s1 = lerp(s01, s11, f.x);
    float4 result = lerp(s0, s1, f.y);

    OutputTexture[outputPos] = result;
}