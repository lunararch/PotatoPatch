#ifndef COMMON_HLSLI
#define COMMON_HLSLI

SamplerState LinearSampler : register(s0);

struct ComputeConstants
{
    float2 inputSize;
    float2 outputSize;
};

#endif