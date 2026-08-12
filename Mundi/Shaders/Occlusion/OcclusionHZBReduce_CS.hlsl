// Normal-Z hierarchy: each parent stores the farthest (maximum) child depth.

cbuffer HZBReduceConstants : register(b0)
{
    uint SourceWidth;
    uint SourceHeight;
    uint DestinationWidth;
    uint DestinationHeight;
};

Texture2D<float> HZBSource : register(t0);
RWTexture2D<float> HZBOutput : register(u0);

[numthreads(8, 8, 1)]
void mainCS(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    const uint2 DestinationPixel = DispatchThreadId.xy;
    if (DestinationPixel.x >= DestinationWidth || DestinationPixel.y >= DestinationHeight)
    {
        return;
    }

    // Proportional ranges preserve the final odd row/column (for example 3 -> 1).
    const uint2 SourceBegin = uint2(
        DestinationPixel.x * SourceWidth / DestinationWidth,
        DestinationPixel.y * SourceHeight / DestinationHeight);
    const uint2 SourceEnd = uint2(
        (DestinationPixel.x + 1) * SourceWidth / DestinationWidth,
        (DestinationPixel.y + 1) * SourceHeight / DestinationHeight);
    float FarthestDepth = 0.0f;

    [loop]
    for (uint SourceY = SourceBegin.y; SourceY < SourceEnd.y; ++SourceY)
    {
        [loop]
        for (uint SourceX = SourceBegin.x; SourceX < SourceEnd.x; ++SourceX)
        {
            FarthestDepth = max(FarthestDepth, HZBSource.Load(int3(uint2(SourceX, SourceY), 0)));
        }
    }

    HZBOutput[DestinationPixel] = FarthestDepth;
}
