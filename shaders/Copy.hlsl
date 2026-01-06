Texture2D<float4> InputTexture : register(t0);
RWTexture2D<float4> OutputTexture : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pos = dispatchThreadID.xy;

    uint2 dimensions;
    InputTexture.GetDimensions(dimensions.x, dimensions.y);

    if (pos.x < dimensions.x && pos.y < dimensions.y)
    {
        OutputTexture[pos] = InputTexture[pos];
    }
}