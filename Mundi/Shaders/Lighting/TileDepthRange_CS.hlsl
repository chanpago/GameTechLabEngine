// Builds one device-depth Min/Max pair for every screen tile.
// Empty tiles are stored as (1, 0), which the light culler treats conservatively.

cbuffer ForwardPlusConstants : register(b0)
{
    row_major float4x4 InverseViewProjection;

    uint ViewportStartX;
    uint ViewportStartY;
    uint ViewportWidth;
    uint ViewportHeight;

    uint TileSize;
    uint TileCountX;
    uint TileCountY;
    uint MaxLightsPerTile;

    uint PointLightCount;
    uint SpotLightCount;
    uint bOrthographic;
    uint Padding0;

    float NearPlane;
    float FarPlane;
    float DepthPadding;
    float Padding1;
};

Texture2D<float> SceneDepth : register(t0);
RWStructuredBuffer<float2> TileDepthRanges : register(u0);

[numthreads(8, 8, 1)]
void mainCS(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    const uint TileX = DispatchThreadId.x;
    const uint TileY = DispatchThreadId.y;
    if (TileX >= TileCountX || TileY >= TileCountY)
    {
        return;
    }

    const uint2 LocalBegin = uint2(TileX, TileY) * TileSize;
    const uint2 LocalEnd = min(LocalBegin + TileSize, uint2(ViewportWidth, ViewportHeight));

    float MinDepth = 1.0f;
    float MaxDepth = 0.0f;
    bool bHasGeometry = false;

    [loop]
    for (uint Y = LocalBegin.y; Y < LocalEnd.y; ++Y)
    {
        [loop]
        for (uint X = LocalBegin.x; X < LocalEnd.x; ++X)
        {
            const uint2 Pixel = uint2(ViewportStartX + X, ViewportStartY + Y);
            const float Depth = SceneDepth.Load(int3(Pixel, 0));
            if (Depth < 0.999999f)
            {
                MinDepth = min(MinDepth, Depth);
                MaxDepth = max(MaxDepth, Depth);
                bHasGeometry = true;
            }
        }
    }

    const uint TileIndex = TileY * TileCountX + TileX;
    TileDepthRanges[TileIndex] = bHasGeometry ? float2(MinDepth, MaxDepth) : float2(1.0f, 0.0f);
}
