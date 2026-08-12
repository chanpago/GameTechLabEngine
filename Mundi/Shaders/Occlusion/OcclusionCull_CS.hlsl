// Tests world-space AABBs against a previous-frame normal-Z HZB.
// Output: 1 = visible, 0 = conservatively occluded.

struct FOcclusionCandidate
{
    float3 BoundsMin;
    uint ComponentIndex;
    float3 BoundsMax;
    uint Padding;
};

cbuffer OcclusionCullConstants : register(b0)
{
    row_major float4x4 ViewProjection;
    row_major float4x4 PreviousViewProjection;

    uint ViewportWidth;
    uint ViewportHeight;
    uint CandidateCount;
    uint HZBMipCount;

    float NearClip;
    float FarClip;
    float WorldDepthBias;
    float MotionPaddingScale;

    uint bPerspectiveProjection;
    uint bUseTemporalMotion;
    float Padding0;
    float Padding1;
};

StructuredBuffer<FOcclusionCandidate> Candidates : register(t0);
Texture2D<float> HZBTexture : register(t1);
RWStructuredBuffer<uint> Visibility : register(u0);

float DeviceDepthToViewDepth(float DeviceDepth)
{
    DeviceDepth = saturate(DeviceDepth);
    if (bPerspectiveProjection != 0)
    {
        // PerspectiveFovLH normal-Z inverse. Device-Z has very little precision
        // in world-distance terms near the far plane, so never bias it directly.
        const float Denominator = max(FarClip - DeviceDepth * (FarClip - NearClip), 1.0e-6f);
        return (NearClip * FarClip) / Denominator;
    }

    return lerp(NearClip, FarClip, DeviceDepth);
}

[numthreads(64, 1, 1)]
void mainCS(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    const uint CandidateIndex = DispatchThreadId.x;
    if (CandidateIndex >= CandidateCount)
    {
        return;
    }

    const FOcclusionCandidate Candidate = Candidates[CandidateIndex];
    float2 MinUV = float2(1.0f, 1.0f);
    float2 MaxUV = float2(0.0f, 0.0f);
    float2 MaxTemporalMotionUV = float2(0.0f, 0.0f);
    float MinDeviceDepth = 1.0f;

    [unroll]
    for (uint CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
    {
        const float3 Corner = float3(
            (CornerIndex & 1) != 0 ? Candidate.BoundsMax.x : Candidate.BoundsMin.x,
            (CornerIndex & 2) != 0 ? Candidate.BoundsMax.y : Candidate.BoundsMin.y,
            (CornerIndex & 4) != 0 ? Candidate.BoundsMax.z : Candidate.BoundsMin.z);
        const float4 Clip = mul(float4(Corner, 1.0f), ViewProjection);

        // Near-plane intersections require edge clipping. Keep them visible.
        if (Clip.w <= 1.0e-5f || Clip.z < 0.0f)
        {
            Visibility[CandidateIndex] = 1;
            return;
        }

        const float3 NDC = Clip.xyz / Clip.w;
        const float2 UV = float2(NDC.x * 0.5f + 0.5f, 0.5f - NDC.y * 0.5f);
        MinUV = min(MinUV, UV);
        MaxUV = max(MaxUV, UV);
        MinDeviceDepth = min(MinDeviceDepth, NDC.z);

        if (bUseTemporalMotion != 0)
        {
            const float4 PreviousClip = mul(float4(Corner, 1.0f), PreviousViewProjection);
            // Crossing the previous near plane is not temporally predictable.
            if (PreviousClip.w <= 1.0e-5f || PreviousClip.z < 0.0f)
            {
                Visibility[CandidateIndex] = 1;
                return;
            }

            const float2 PreviousNDC = PreviousClip.xy / PreviousClip.w;
            const float2 PreviousUV = float2(
                PreviousNDC.x * 0.5f + 0.5f,
                0.5f - PreviousNDC.y * 0.5f);
            MaxTemporalMotionUV = max(MaxTemporalMotionUV, abs(UV - PreviousUV));
        }
    }

    if (MaxUV.x <= 0.0f || MaxUV.y <= 0.0f || MinUV.x >= 1.0f || MinUV.y >= 1.0f)
    {
        Visibility[CandidateIndex] = 1;
        return;
    }

    // Predict one more frame of similar motion. Expanding the candidate makes
    // edge disocclusion fail open because any uncovered HZB texel keeps it visible.
    const float2 OnePixelUV = 1.0f / float2(max(ViewportWidth, 1u), max(ViewportHeight, 1u));
    const float2 TemporalPaddingUV = MaxTemporalMotionUV * MotionPaddingScale + OnePixelUV;
    MinUV = saturate(MinUV - TemporalPaddingUV);
    MaxUV = saturate(MaxUV + TemporalPaddingUV);
    const float2 PixelExtent = max((MaxUV - MinUV) * float2(ViewportWidth, ViewportHeight), 1.0f.xx);
    const float MaxExtent = max(PixelExtent.x, PixelExtent.y);
    // Use one finer mip than a one-texel footprint. This normally checks a
    // 2x2 to 3x3 footprint and prevents a single coarse cell from hiding useful depth.
    const float BaseMip = floor(log2(max(MaxExtent, 1.0f)));
    uint MipLevel = (uint)max(BaseMip - 1.0f, 0.0f);
    MipLevel = min(MipLevel, HZBMipCount - 1);

    uint MipWidth = 1;
    uint MipHeight = 1;
    uint AvailableMipCount = 1;
    HZBTexture.GetDimensions(MipLevel, MipWidth, MipHeight, AvailableMipCount);

    const uint2 MinTexel = min(
        uint2(floor(MinUV * float2(MipWidth, MipHeight))),
        uint2(MipWidth - 1, MipHeight - 1));
    const uint2 MaxTexel = min(
        uint2(max(ceil(MaxUV * float2(MipWidth, MipHeight)) - 1.0f, 0.0f.xx)),
        uint2(MipWidth - 1, MipHeight - 1));
    const float CandidateNearestViewDepth = DeviceDepthToViewDepth(MinDeviceDepth);

    [loop]
    for (uint Y = MinTexel.y; Y <= MaxTexel.y; ++Y)
    {
        [loop]
        for (uint X = MinTexel.x; X <= MaxTexel.x; ++X)
        {
            const float FarthestOccluderDepth = HZBTexture.Load(int3(uint2(X, Y), MipLevel));
            const float FarthestOccluderViewDepth = DeviceDepthToViewDepth(FarthestOccluderDepth);
            if (FarthestOccluderViewDepth + WorldDepthBias >= CandidateNearestViewDepth)
            {
                Visibility[CandidateIndex] = 1;
                return;
            }
        }
    }

    Visibility[CandidateIndex] = 0;
}
