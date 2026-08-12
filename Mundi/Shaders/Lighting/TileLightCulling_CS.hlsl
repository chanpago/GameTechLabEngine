// Creates the Forward+ light list for each depth-bounded screen tile.

#include "../Common/LightStructures.hlsl"

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

StructuredBuffer<float2> TileDepthRanges : register(t0);
StructuredBuffer<FPointLightInfo> PointLights : register(t1);
StructuredBuffer<FSpotLightInfo> SpotLights : register(t2);
RWStructuredBuffer<uint> TileLightIndices : register(u0);

float3 Unproject(float2 NDC, float DeviceDepth)
{
    float4 WorldPosition = mul(float4(NDC, DeviceDepth, 1.0f), InverseViewProjection);
    return WorldPosition.xyz / WorldPosition.w;
}

float4 MakeInwardPlane(float3 A, float3 B, float3 C, float3 InsidePoint)
{
    float3 Normal = normalize(cross(B - A, C - A));
    float Distance = -dot(Normal, A);
    float4 Plane = float4(Normal, Distance);
    if (dot(Plane, float4(InsidePoint, 1.0f)) < 0.0f)
    {
        Plane = -Plane;
    }
    return Plane;
}

bool SphereIntersectsFrustum(float3 Center, float Radius, float4 Planes[6])
{
    [unroll]
    for (uint PlaneIndex = 0; PlaneIndex < 6; ++PlaneIndex)
    {
        if (dot(Planes[PlaneIndex], float4(Center, 1.0f)) < -Radius)
        {
            return false;
        }
    }
    return true;
}

[numthreads(8, 8, 1)]
void mainCS(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    const uint TileX = DispatchThreadId.x;
    const uint TileY = DispatchThreadId.y;
    if (TileX >= TileCountX || TileY >= TileCountY)
    {
        return;
    }

    const uint TileIndex = TileY * TileCountX + TileX;
    const uint TileOffset = TileIndex * MaxLightsPerTile;
    const float2 StoredDepthRange = TileDepthRanges[TileIndex];

    // No opaque depth can still contain a billboard/translucent surface. Use the full
    // camera range for that rare case so Forward+ never removes a required light.
    const bool bEmptyTile = StoredDepthRange.x > StoredDepthRange.y;
    const float MinDepth = bEmptyTile ? 0.0f : max(0.0f, StoredDepthRange.x - DepthPadding);
    const float MaxDepth = bEmptyTile ? 1.0f : min(1.0f, StoredDepthRange.y + DepthPadding);

    const float PixelMinX = float(TileX * TileSize);
    const float PixelMaxX = float(min((TileX + 1) * TileSize, ViewportWidth));
    const float PixelMinY = float(TileY * TileSize);
    const float PixelMaxY = float(min((TileY + 1) * TileSize, ViewportHeight));

    const float NDCMinX = PixelMinX / float(ViewportWidth) * 2.0f - 1.0f;
    const float NDCMaxX = PixelMaxX / float(ViewportWidth) * 2.0f - 1.0f;
    const float NDCMaxY = 1.0f - PixelMinY / float(ViewportHeight) * 2.0f;
    const float NDCMinY = 1.0f - PixelMaxY / float(ViewportHeight) * 2.0f;

    float3 Corners[8];
    Corners[0] = Unproject(float2(NDCMinX, NDCMaxY), MinDepth); // near top-left
    Corners[1] = Unproject(float2(NDCMaxX, NDCMaxY), MinDepth); // near top-right
    Corners[2] = Unproject(float2(NDCMaxX, NDCMinY), MinDepth); // near bottom-right
    Corners[3] = Unproject(float2(NDCMinX, NDCMinY), MinDepth); // near bottom-left
    Corners[4] = Unproject(float2(NDCMinX, NDCMaxY), MaxDepth); // far top-left
    Corners[5] = Unproject(float2(NDCMaxX, NDCMaxY), MaxDepth); // far top-right
    Corners[6] = Unproject(float2(NDCMaxX, NDCMinY), MaxDepth); // far bottom-right
    Corners[7] = Unproject(float2(NDCMinX, NDCMinY), MaxDepth); // far bottom-left

    float3 InsidePoint = 0.0f;
    [unroll]
    for (uint CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
    {
        InsidePoint += Corners[CornerIndex];
    }
    InsidePoint *= 0.125f;

    float4 Planes[6];
    Planes[0] = MakeInwardPlane(Corners[0], Corners[3], Corners[7], InsidePoint); // left
    Planes[1] = MakeInwardPlane(Corners[2], Corners[1], Corners[5], InsidePoint); // right
    Planes[2] = MakeInwardPlane(Corners[1], Corners[0], Corners[4], InsidePoint); // top
    Planes[3] = MakeInwardPlane(Corners[3], Corners[2], Corners[6], InsidePoint); // bottom
    Planes[4] = MakeInwardPlane(Corners[0], Corners[1], Corners[2], InsidePoint); // near
    Planes[5] = MakeInwardPlane(Corners[5], Corners[4], Corners[7], InsidePoint); // far

    uint LightCount = 0;
    const uint MaximumStoredLights = MaxLightsPerTile - 1;

    [loop]
    for (uint PointIndex = 0; PointIndex < PointLightCount && LightCount < MaximumStoredLights; ++PointIndex)
    {
        FPointLightInfo Light = PointLights[PointIndex];
        if (SphereIntersectsFrustum(Light.Position, Light.AttenuationRadius, Planes))
        {
            TileLightIndices[TileOffset + 1 + LightCount] = PointIndex;
            ++LightCount;
        }
    }

    [loop]
    for (uint SpotIndex = 0; SpotIndex < SpotLightCount && LightCount < MaximumStoredLights; ++SpotIndex)
    {
        FSpotLightInfo Light = SpotLights[SpotIndex];
        // Conservative bounding sphere. The pixel shader still performs the exact cone test.
        if (SphereIntersectsFrustum(Light.Position, Light.AttenuationRadius, Planes))
        {
            TileLightIndices[TileOffset + 1 + LightCount] = (1u << 16) | (SpotIndex & 0xffffu);
            ++LightCount;
        }
    }

    TileLightIndices[TileOffset] = LightCount;
}
