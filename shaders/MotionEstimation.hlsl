Texture2D<float4> CurrentFrame : register(t0);
Texture2D<float4> PreviousFrame : register(t1);
RWTexture2D<float2> MotionVectors : register(u0);

cbuffer Constants : register(b0)
{
    uint2 dimensions;
    uint blockSize;
    uint searchRange;
};

// Simple block matching
float BlockError(int2 pos, int2 offset)
{
    float error = 0.0f;

    for (int y = 0; y < blockSize; y++)
    {
        for (int x = 0; x < blockSize; x++)
        {
            int2 curr = pos + int2(x, y);
            int2 prev = curr + offset;

            if (prev.x >= 0 && prev.y >= 0 && prev.x < dimensions.x && prev.y < dimensions.y)
            {
                float4 c0 = CurrentFrame[curr];
                float4 c1 = PreviousFrame[prev];
                float4 diff = c0 - c1;
                error += dot(diff.rgb, diff.rgb);
            }
        }
    }

    return error;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pos = dispatchThreadID.xy * blockSize;

    if (pos.x >= dimensions.x || pos.y >= dimensions.y)
        return;

    float2 bestMotion = float2(0, 0);
    float bestError = 1e10;

    // Search in a window around current position
    for (int dy = -searchRange; dy <= searchRange; dy++)
    {
        for (int dx = -searchRange; dx <= searchRange; dx++)
        {
            float error = BlockError(pos, int2(dx, dy));

            if (error < bestError)
            {
                bestError = error;
                bestMotion = float2(dx, dy);
            }
        }
    }

    MotionVectors[pos / blockSize] = bestMotion;
}