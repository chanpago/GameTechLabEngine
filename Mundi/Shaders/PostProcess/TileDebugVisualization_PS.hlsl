// Forward+ debug overlay.
// Mode 1: tile light-count heatmap
// Mode 2: tile depth range (left=min, right=max)
// Mode 3: heatmap plus a depth-range strip at the bottom of every tile

cbuffer TileCullingBuffer : register(b11)
{
    uint TileSize;
    uint TileCountX;
    uint TileCountY;
    uint bUseTileCulling;
    uint ViewportStartX;
    uint ViewportStartY;
    uint bUseDepthBounds;
    uint TileDebugMode;
    float TileNearClip;
    float TileFarClip;
    uint bTileOrthographic;
    uint TilePadding;
};

Texture2D g_SceneTexture : register(t0);
SamplerState g_SamplerLinear : register(s0);
StructuredBuffer<uint> g_TileLightIndices : register(t2);
StructuredBuffer<float2> g_TileDepthRanges : register(t5);

float3 LightCountToHeatmap(uint LightCount)
{
    float T = saturate(float(LightCount) / 16.0f);
    if (T < 0.25f)
        return lerp(float3(0.0f, 0.05f, 0.8f), float3(0.0f, 0.9f, 1.0f), T * 4.0f);
    if (T < 0.5f)
        return lerp(float3(0.0f, 0.9f, 1.0f), float3(0.0f, 1.0f, 0.1f), (T - 0.25f) * 4.0f);
    if (T < 0.75f)
        return lerp(float3(0.0f, 1.0f, 0.1f), float3(1.0f, 0.95f, 0.0f), (T - 0.5f) * 4.0f);
    return lerp(float3(1.0f, 0.95f, 0.0f), float3(1.0f, 0.0f, 0.0f), (T - 0.75f) * 4.0f);
}

float DeviceDepthToViewDistance(float DeviceDepth)
{
    if (bTileOrthographic != 0)
    {
        return lerp(TileNearClip, TileFarClip, DeviceDepth);
    }
    return (TileNearClip * TileFarClip) /
        max(TileFarClip - DeviceDepth * (TileFarClip - TileNearClip), 0.00001f);
}

float NormalizeViewDistance(float DeviceDepth)
{
    float Distance = max(DeviceDepthToViewDistance(DeviceDepth), TileNearClip);
    float Range = max(TileFarClip / max(TileNearClip, 0.0001f), 1.0f);
    return saturate(log2(Distance / max(TileNearClip, 0.0001f)) / max(log2(Range), 0.0001f));
}

float3 DepthColor(float DeviceDepth, bool bMaximum)
{
    float T = NormalizeViewDistance(DeviceDepth);
    float3 NearColor = bMaximum ? float3(0.35f, 0.02f, 0.0f) : float3(0.0f, 0.04f, 0.35f);
    float3 FarColor = bMaximum ? float3(1.0f, 0.85f, 0.05f) : float3(0.05f, 1.0f, 1.0f);
    return lerp(NearColor, FarColor, T);
}

float4 mainPS(float4 Position : SV_Position, float2 TexCoord : TEXCOORD0) : SV_Target
{
    float3 SceneColor = g_SceneTexture.Sample(g_SamplerLinear, TexCoord).rgb;
    if (bUseTileCulling == 0 || bUseDepthBounds == 0 || TileDebugMode == 0)
    {
        return float4(SceneColor, 1.0f);
    }

    uint2 LocalPixel = uint2(Position.xy) - uint2(ViewportStartX, ViewportStartY);
    uint2 TileCoordinate = LocalPixel / TileSize;
    if (TileCoordinate.x >= TileCountX || TileCoordinate.y >= TileCountY)
    {
        return float4(SceneColor, 1.0f);
    }

    uint TileIndex = TileCoordinate.y * TileCountX + TileCoordinate.x;
    uint TileOffset = TileIndex * 256;
    uint LightCount = g_TileLightIndices[TileOffset];
    float2 DepthRange = g_TileDepthRanges[TileIndex];
    bool bEmpty = DepthRange.x > DepthRange.y;
    uint2 TileLocalPixel = LocalPixel % TileSize;
    bool bBorder = TileLocalPixel.x == 0 || TileLocalPixel.y == 0;

    float3 Result = SceneColor;
    uint DepthStripStart = TileSize > 3 ? TileSize - 3 : 0;
    if (TileDebugMode == 1 || TileDebugMode == 3)
    {
        Result = lerp(Result, LightCountToHeatmap(LightCount), 0.35f);
    }

    if (TileDebugMode == 2)
    {
        if (bEmpty)
        {
            Result = lerp(Result, float3(0.85f, 0.0f, 0.85f), 0.65f);
        }
        else
        {
            bool bShowMaximum = TileLocalPixel.x >= TileSize / 2;
            float Depth = bShowMaximum ? DepthRange.y : DepthRange.x;
            Result = lerp(Result, DepthColor(Depth, bShowMaximum), 0.72f);
        }
    }
    else if (TileDebugMode == 3 && TileLocalPixel.y >= DepthStripStart)
    {
        if (bEmpty)
        {
            Result = float3(0.85f, 0.0f, 0.85f);
        }
        else
        {
            bool bShowMaximum = TileLocalPixel.x >= TileSize / 2;
            Result = DepthColor(bShowMaximum ? DepthRange.y : DepthRange.x, bShowMaximum);
        }
    }

    if (bBorder)
    {
        Result = lerp(Result, float3(1.0f, 1.0f, 1.0f), 0.45f);
    }
    return float4(Result, 1.0f);
}
