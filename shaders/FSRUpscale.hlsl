#include "Common.hlsli"

Texture2D<float4> InputTexture : register(t0);
RWTexture2D<float4> OutputTexture : register(u0);

cbuffer Constants : register(b0)
{
    float2 inputSize;
    float2 outputSize;
};

// Simplified FSR-inspired edge-adaptive upscaling
float4 FSRFilter(float2 uv, float2 inputSize)
{
    float2 texelSize = 1.0f / inputSize;

    // Sample 3x3 neighborhood
    float4 samples[9];
    int idx = 0;
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            float2 offset = float2(x, y) * texelSize;
            samples[idx++] = InputTexture.SampleLevel(LinearSampler, uv + offset, 0);
        }
    }

    // Detect edges by comparing luminance
    float3 weights = float3(0.299f, 0.587f, 0.114f);

    float center = dot(samples[4].rgb, weights);
    float edges = 0.0f;

    for (int i = 0; i < 9; i++)
    {
        if (i != 4)
        {
            float lum = dot(samples[i].rgb, weights);
            edges += abs(center - lum);
        }
    }

    edges = saturate(edges * 2.0f);

    // Blend between sharp and smooth based on edge detection
    float4 sharp = samples[4];
    float4 smooth = (samples[0] + samples[2] + samples[6] + samples[8]) * 0.0625f +
                    (samples[1] + samples[3] + samples[5] + samples[7]) * 0.125f +
                    samples[4] * 0.25f;

    return lerp(smooth, sharp, edges);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 outputPos = dispatchThreadID.xy;

    if (outputPos.x >= outputSize.x || outputPos.y >= outputSize.y)
        return;

    float2 uv = (float2(outputPos) + 0.5f) / outputSize;
    float4 color = FSRFilter(uv, inputSize);

    OutputTexture[outputPos] = color;
}
