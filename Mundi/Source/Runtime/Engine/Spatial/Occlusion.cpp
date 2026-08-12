#include "pch.h"
#include "Occlusion.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace
{
    constexpr float ProjectionEpsilon = 1.0e-5f;
    constexpr int MaxHZBSamplesPerCandidate = 16;

    inline float Clamp01(float Value)
    {
        return std::max(0.0f, std::min(1.0f, Value));
    }

    inline float LinearizeZ01(float ViewZ, float NearClip, float FarClip)
    {
        const float DepthRange = FarClip - NearClip;
        if (DepthRange <= ProjectionEpsilon)
        {
            return 1.0f;
        }
        return Clamp01((ViewZ - NearClip) / DepthRange);
    }

    void MakeAabbCornersMinMax(const FAABB& Bound, FVector Corners[8])
    {
        const FVector& Min = Bound.Min;
        const FVector& Max = Bound.Max;
        Corners[0] = { Min.X, Min.Y, Min.Z };
        Corners[1] = { Max.X, Min.Y, Min.Z };
        Corners[2] = { Min.X, Max.Y, Min.Z };
        Corners[3] = { Max.X, Max.Y, Min.Z };
        Corners[4] = { Min.X, Min.Y, Max.Z };
        Corners[5] = { Max.X, Min.Y, Max.Z };
        Corners[6] = { Min.X, Max.Y, Max.Z };
        Corners[7] = { Max.X, Max.Y, Max.Z };
    }
}

void FOcclusionGrid::Initialize(int InWidth, int InHeight)
{
    Width = std::max(1, InWidth);
    Height = std::max(1, InHeight);
    Depth.assign(static_cast<size_t>(Width) * Height, 1.0f);

    BuildLevels.clear();
    LevelWidths.clear();
    LevelHeights.clear();

    int LevelWidth = Width;
    int LevelHeight = Height;
    while (true)
    {
        LevelWidths.Add(LevelWidth);
        LevelHeights.Add(LevelHeight);
        BuildLevels.Add(TArray<float>(static_cast<size_t>(LevelWidth) * LevelHeight, 1.0f));

        if (LevelWidth == 1 && LevelHeight == 1)
        {
            break;
        }

        // 홀수 크기의 마지막 행/열이 유실되지 않도록 ceil-divide 합니다.
        LevelWidth = std::max(1, (LevelWidth + 1) / 2);
        LevelHeight = std::max(1, (LevelHeight + 1) / 2);
    }

    bHZBValid = false;
}

void FOcclusionGrid::Clear()
{
    std::fill(Depth.begin(), Depth.end(), 1.0f);
    bHZBValid = false;
}

bool FOcclusionGrid::ComputeConservativeRasterBounds(
    const FOcclusionRect& Rect,
    float ErodeScale,
    int& OutMinX,
    int& OutMinY,
    int& OutMaxX,
    int& OutMaxY) const
{
    if (Width <= 0 || Height <= 0 || Rect.MinX >= Rect.MaxX || Rect.MinY >= Rect.MaxY)
    {
        return false;
    }

    ErodeScale = std::max(0.0f, std::min(1.0f, ErodeScale));
    const float CenterX = 0.5f * (Rect.MinX + Rect.MaxX);
    const float CenterY = 0.5f * (Rect.MinY + Rect.MaxY);
    const float HalfWidth = 0.5f * (Rect.MaxX - Rect.MinX) * ErodeScale;
    const float HalfHeight = 0.5f * (Rect.MaxY - Rect.MinY) * ErodeScale;

    const float ErodedMinX = Clamp01(CenterX - HalfWidth);
    const float ErodedMinY = Clamp01(CenterY - HalfHeight);
    const float ErodedMaxX = Clamp01(CenterX + HalfWidth);
    const float ErodedMaxY = Clamp01(CenterY + HalfHeight);

    // 셀 전체가 eroded rect 안에 포함될 때만 오클루더 깊이를 기록합니다.
    OutMinX = static_cast<int>(std::ceil(ErodedMinX * Width));
    OutMinY = static_cast<int>(std::ceil(ErodedMinY * Height));
    OutMaxX = static_cast<int>(std::floor(ErodedMaxX * Width)) - 1;
    OutMaxY = static_cast<int>(std::floor(ErodedMaxY * Height)) - 1;

    OutMinX = std::max(0, std::min(Width - 1, OutMinX));
    OutMinY = std::max(0, std::min(Height - 1, OutMinY));
    OutMaxX = std::max(0, std::min(Width - 1, OutMaxX));
    OutMaxY = std::max(0, std::min(Height - 1, OutMaxY));

    // Screen size is an occluder priority, not a hard exclusion rule. A small
    // opaque mesh may still contribute one conservative cell, allowing many
    // nearby meshes to build a useful depth surface together.
    return OutMinX <= OutMaxX && OutMinY <= OutMaxY;
}

bool FOcclusionGrid::CanRasterizeConservative(const FOcclusionRect& Rect, float ErodeScale) const
{
    int MinX = 0;
    int MinY = 0;
    int MaxX = -1;
    int MaxY = -1;
    return ComputeConservativeRasterBounds(Rect, ErodeScale, MinX, MinY, MaxX, MaxY);
}

bool FOcclusionGrid::RasterizeConservative(const FOcclusionRect& Rect, float ErodeScale)
{
    int MinX = 0;
    int MinY = 0;
    int MaxX = -1;
    int MaxY = -1;
    if (!ComputeConservativeRasterBounds(Rect, ErodeScale, MinX, MinY, MaxX, MaxY))
    {
        return false;
    }

    bool bDepthChanged = false;
    for (int Y = MinY; Y <= MaxY; ++Y)
    {
        float* Row = &Depth[static_cast<size_t>(Y) * Width];
        for (int X = MinX; X <= MaxX; ++X)
        {
            // 겹치는 오클루더 중 카메라에 가장 가까운 보수적 far depth를 유지합니다.
            if (Rect.MaxZ < Row[X])
            {
                Row[X] = Rect.MaxZ;
                bDepthChanged = true;
            }
        }
    }

    if (bDepthChanged)
    {
        bHZBValid = false;
    }
    return bDepthChanged;
}

void FOcclusionGrid::BuildHZB()
{
    if (BuildLevels.IsEmpty())
    {
        return;
    }

    BuildLevels[0] = Depth;

    for (uint32 LevelIndex = 1; LevelIndex < static_cast<uint32>(BuildLevels.Num()); ++LevelIndex)
    {
        const TArray<float>& Previous = BuildLevels[LevelIndex - 1];
        TArray<float>& Current = BuildLevels[LevelIndex];
        const int PreviousWidth = LevelWidths[LevelIndex - 1];
        const int PreviousHeight = LevelHeights[LevelIndex - 1];
        const int CurrentWidth = LevelWidths[LevelIndex];
        const int CurrentHeight = LevelHeights[LevelIndex];

        for (int Y = 0; Y < CurrentHeight; ++Y)
        {
            for (int X = 0; X < CurrentWidth; ++X)
            {
                float MaxDepth = 0.0f;
                for (int OffsetY = 0; OffsetY < 2; ++OffsetY)
                {
                    const int SourceY = Y * 2 + OffsetY;
                    if (SourceY >= PreviousHeight)
                    {
                        continue;
                    }

                    for (int OffsetX = 0; OffsetX < 2; ++OffsetX)
                    {
                        const int SourceX = X * 2 + OffsetX;
                        if (SourceX >= PreviousWidth)
                        {
                            continue;
                        }

                        MaxDepth = std::max(MaxDepth, Previous[static_cast<size_t>(SourceY) * PreviousWidth + SourceX]);
                    }
                }
                Current[static_cast<size_t>(Y) * CurrentWidth + X] = MaxDepth;
            }
        }
    }

    bHZBValid = true;
}

bool FOcclusionGrid::IsRectOccluded(const FOcclusionRect& Rect, float DepthBias) const
{
    if (!bHZBValid || BuildLevels.IsEmpty() || Rect.MinX >= Rect.MaxX || Rect.MinY >= Rect.MaxY)
    {
        return false;
    }

    int SelectedLevel = 0;
    int SelectedMinX = 0;
    int SelectedMinY = 0;
    int SelectedMaxX = 0;
    int SelectedMaxY = 0;

    // 관련 셀을 전부 검사하되 최대 16개 이하가 되는 가장 세밀한 mip을 선택합니다.
    for (uint32 LevelIndex = 0; LevelIndex < static_cast<uint32>(BuildLevels.Num()); ++LevelIndex)
    {
        const int LevelWidth = LevelWidths[LevelIndex];
        const int LevelHeight = LevelHeights[LevelIndex];
        const int MinX = std::max(0, std::min(LevelWidth - 1, static_cast<int>(std::floor(Rect.MinX * LevelWidth))));
        const int MinY = std::max(0, std::min(LevelHeight - 1, static_cast<int>(std::floor(Rect.MinY * LevelHeight))));
        const int MaxX = std::max(0, std::min(LevelWidth - 1, static_cast<int>(std::ceil(Rect.MaxX * LevelWidth)) - 1));
        const int MaxY = std::max(0, std::min(LevelHeight - 1, static_cast<int>(std::ceil(Rect.MaxY * LevelHeight)) - 1));
        const int SampleCount = std::max(1, MaxX - MinX + 1) * std::max(1, MaxY - MinY + 1);

        SelectedLevel = static_cast<int>(LevelIndex);
        SelectedMinX = MinX;
        SelectedMinY = MinY;
        SelectedMaxX = MaxX;
        SelectedMaxY = MaxY;
        if (SampleCount <= MaxHZBSamplesPerCandidate)
        {
            break;
        }
    }

    const TArray<float>& Level = BuildLevels[SelectedLevel];
    const int LevelWidth = LevelWidths[SelectedLevel];
    for (int Y = SelectedMinY; Y <= SelectedMaxY; ++Y)
    {
        const float* Row = &Level[static_cast<size_t>(Y) * LevelWidth];
        for (int X = SelectedMinX; X <= SelectedMaxX; ++X)
        {
            // MAX HZB에서 하나라도 후보보다 멀거나 비어 있으면 완전 가림이 아닙니다.
            if (Row[X] + DepthBias > Rect.MinZ)
            {
                return false;
            }
        }
    }

    return true;
}

bool FOcclusionCullingManagerCPU::ComputeRectAndMinZ(
    const FCandidateDrawable& Candidate,
    int /*ViewW*/,
    int /*ViewH*/,
    FOcclusionRect& OutRect)
{
    FVector Corners[8];
    MakeAabbCornersMinMax(Candidate.Bound, Corners);

    float MinX = FLT_MAX;
    float MinY = FLT_MAX;
    float MaxX = -FLT_MAX;
    float MaxY = -FLT_MAX;
    float MinZ = FLT_MAX;
    float MaxZ = -FLT_MAX;

    for (const FVector& Corner : Corners)
    {
        const float Point[4] = { Corner.X, Corner.Y, Corner.Z, 1.0f };

        float ViewPoint[4];
        MulPointRow(Point, Candidate.WorldView, ViewPoint);

        float ClipPoint[4];
        MulPointRow(Point, Candidate.WorldViewProj, ClipPoint);

        // near plane과 교차하는 AABB는 사각형 edge clipping 없이는 안전하게 투영할 수 없습니다.
        // 이런 후보는 이번 프레임에 보이는 것으로 유지하고 오클루더로도 사용하지 않습니다.
        if (ViewPoint[2] <= Candidate.NearClip || ClipPoint[3] <= ProjectionEpsilon)
        {
            return false;
        }

        const float InverseW = 1.0f / ClipPoint[3];
        const float U = 0.5f * (ClipPoint[0] * InverseW + 1.0f);
        const float V = 0.5f * (ClipPoint[1] * InverseW + 1.0f);

        MinX = std::min(MinX, U);
        MinY = std::min(MinY, V);
        MaxX = std::max(MaxX, U);
        MaxY = std::max(MaxY, V);

        const float LinearDepth = LinearizeZ01(ViewPoint[2], Candidate.NearClip, Candidate.FarClip);
        MinZ = std::min(MinZ, LinearDepth);
        MaxZ = std::max(MaxZ, LinearDepth);
    }

    if (MaxX <= 0.0f || MaxY <= 0.0f || MinX >= 1.0f || MinY >= 1.0f)
    {
        return false;
    }

    OutRect.MinX = Clamp01(MinX);
    OutRect.MinY = Clamp01(MinY);
    OutRect.MaxX = Clamp01(MaxX);
    OutRect.MaxY = Clamp01(MaxY);
    OutRect.MinZ = Clamp01(MinZ);
    OutRect.MaxZ = Clamp01(MaxZ);
    OutRect.ActorIndex = Candidate.ActorIndex;
    return OutRect.MinX < OutRect.MaxX && OutRect.MinY < OutRect.MaxY;
}

void FOcclusionCullingManagerCPU::MarkVisible(uint32 ActorIndex)
{
    if (static_cast<uint32>(OccludedStreak.Num()) <= ActorIndex)
    {
        OccludedStreak.resize(static_cast<size_t>(ActorIndex) + 1, 0);
    }
    if (static_cast<uint32>(LastState.Num()) <= ActorIndex)
    {
        LastState.resize(static_cast<size_t>(ActorIndex) + 1, 1);
    }

    OccludedStreak[ActorIndex] = 0;
    LastState[ActorIndex] = 1;
}

bool FOcclusionCullingManagerCPU::ApplyOccludedHysteresis(uint32 ActorIndex, bool bRawOccluded)
{
    if (static_cast<uint32>(OccludedStreak.Num()) <= ActorIndex)
    {
        OccludedStreak.resize(static_cast<size_t>(ActorIndex) + 1, 0);
    }
    if (static_cast<uint32>(LastState.Num()) <= ActorIndex)
    {
        LastState.resize(static_cast<size_t>(ActorIndex) + 1, 1);
    }

    // 다시 보이는 경우에는 즉시 복구합니다. 카메라 이동 시 이전 프레임 상태가 물체를 숨기지 않습니다.
    if (!bRawOccluded)
    {
        MarkVisible(ActorIndex);
        return false;
    }

    if (OccludedStreak[ActorIndex] < 255)
    {
        ++OccludedStreak[ActorIndex];
    }
    const bool bWasOccluded = LastState[ActorIndex] == 0;
    const bool bFinalOccluded = bWasOccluded || OccludedStreak[ActorIndex] >= OccludedFrameThreshold;
    LastState[ActorIndex] = bFinalOccluded ? 0 : 1;
    return bFinalOccluded;
}

void FOcclusionCullingManagerCPU::BeginFrameStats(
    uint32 RegisteredMeshCount,
    uint32 FrustumVisibleCount,
    bool bFrustumEnabled,
    bool bOcclusionEnabled,
    bool bMaterialSortingEnabled)
{
    LastStats = {};
    LastStats.RegisteredMeshCount = RegisteredMeshCount;
    LastStats.FrustumVisibleCount = FrustumVisibleCount;
    LastStats.FrustumCulledCount = RegisteredMeshCount > FrustumVisibleCount
        ? RegisteredMeshCount - FrustumVisibleCount
        : 0;
    LastStats.FinalVisibleCount = FrustumVisibleCount;
    LastStats.bFrustumEnabled = bFrustumEnabled;
    LastStats.bOcclusionEnabled = bOcclusionEnabled;
    LastStats.bMaterialSortingEnabled = bMaterialSortingEnabled;
}

void FOcclusionCullingManagerCPU::CullFrontToBack(
    const TArray<FCandidateDrawable>& Candidates,
    int ViewW,
    int ViewH,
    TArray<uint8_t>& OutVisibleFlags)
{
    const auto StartTime = std::chrono::steady_clock::now();
    auto FinishStats = [this, StartTime]()
    {
        const auto EndTime = std::chrono::steady_clock::now();
        LastStats.CPUTimeMs = std::chrono::duration<float, std::milli>(EndTime - StartTime).count();
    };

    LastStats.CandidateCount = 0;
    LastStats.ProjectedCount = 0;
    LastStats.OccluderCount = 0;
    LastStats.CulledCount = 0;
    LastStats.CPUTimeMs = 0.0f;
    LastStats.CandidateCount = static_cast<uint32>(Candidates.Num());

    if (Candidates.IsEmpty())
    {
        OutVisibleFlags.clear();
        FinishStats();
        return;
    }

    uint32 MaxActorIndex = 0;
    for (const FCandidateDrawable& Candidate : Candidates)
    {
        MaxActorIndex = std::max(MaxActorIndex, Candidate.ActorIndex);
    }
    OutVisibleFlags.assign(static_cast<size_t>(MaxActorIndex) + 1, 1);

    TArray<FProjectedCandidate> ProjectedCandidates;
    ProjectedCandidates.Reserve(Candidates.Num());
    uint32 PotentialOccluderCount = 0;

    for (const FCandidateDrawable& Candidate : Candidates)
    {
        FOcclusionRect Rect;
        if (!ComputeRectAndMinZ(Candidate, ViewW, ViewH, Rect))
        {
            MarkVisible(Candidate.ActorIndex);
            continue;
        }

        FProjectedCandidate Projected;
        Projected.Rect = Rect;
        const float ProjectedWidthPixels = (Rect.MaxX - Rect.MinX) * static_cast<float>(std::max(ViewW, 1));
        const float ProjectedHeightPixels = (Rect.MaxY - Rect.MinY) * static_cast<float>(std::max(ViewH, 1));
        Projected.ScreenAreaPixels = ProjectedWidthPixels * ProjectedHeightPixels;
        Projected.bCanOcclude = Candidate.bCanOcclude && Grid.CanRasterizeConservative(Rect);
        if (Projected.bCanOcclude)
        {
            ++PotentialOccluderCount;
        }
        ProjectedCandidates.Add(Projected);
    }

    LastStats.ProjectedCount = static_cast<uint32>(ProjectedCandidates.Num());
    if (ProjectedCandidates.IsEmpty() || PotentialOccluderCount == 0)
    {
        for (const FProjectedCandidate& Candidate : ProjectedCandidates)
        {
            MarkVisible(Candidate.Rect.ActorIndex);
        }
        FinishStats();
        return;
    }

    ProjectedCandidates.Sort([](const FProjectedCandidate& Left, const FProjectedCandidate& Right)
    {
        if (Left.Rect.MinZ == Right.Rect.MinZ)
        {
            return Left.Rect.ActorIndex < Right.Rect.ActorIndex;
        }
        return Left.Rect.MinZ < Right.Rect.MinZ;
    });

    Grid.Clear();
    bool bHasOccluderDepth = false;
    bool bDepthDirty = false;
    uint32 OccluderCount = 0;

    for (uint32 BatchStart = 0; BatchStart < static_cast<uint32>(ProjectedCandidates.Num()); BatchStart += CandidateBatchSize)
    {
        const uint32 BatchEnd = std::min<uint32>(
            static_cast<uint32>(ProjectedCandidates.Num()),
            BatchStart + CandidateBatchSize);

        if (bHasOccluderDepth && bDepthDirty)
        {
            Grid.BuildHZB();
            bDepthDirty = false;
        }

        for (uint32 Index = BatchStart; Index < BatchEnd; ++Index)
        {
            FProjectedCandidate& Candidate = ProjectedCandidates[Index];
            Candidate.bRawOccluded = bHasOccluderDepth && Grid.IsRectOccluded(Candidate.Rect, 2.0e-3f);

            const bool bFinalOccluded = ApplyOccludedHysteresis(Candidate.Rect.ActorIndex, Candidate.bRawOccluded);
            OutVisibleFlags[Candidate.Rect.ActorIndex] = bFinalOccluded ? 0 : 1;
            if (bFinalOccluded)
            {
                ++LastStats.CulledCount;
            }
        }

        // 같은 depth batch 안에서는 서로를 가리지 않게 하여 정렬 오차에 의한 오버컬링을 막습니다.
        TArray<uint32> OccluderOrder;
        OccluderOrder.Reserve(BatchEnd - BatchStart);
        for (uint32 Index = BatchStart; Index < BatchEnd; ++Index)
        {
            const FProjectedCandidate& Candidate = ProjectedCandidates[Index];
            if (!Candidate.bRawOccluded && Candidate.bCanOcclude)
            {
                OccluderOrder.Add(Index);
            }
        }

        // Culling remains front-to-back. Within one depth batch, screen area
        // controls which candidates improve the HZB first. There is no fixed
        // occluder cap; only candidates that actually lower a depth cell count.
        OccluderOrder.Sort([&ProjectedCandidates](uint32 LeftIndex, uint32 RightIndex)
        {
            const FProjectedCandidate& Left = ProjectedCandidates[LeftIndex];
            const FProjectedCandidate& Right = ProjectedCandidates[RightIndex];
            if (Left.ScreenAreaPixels == Right.ScreenAreaPixels)
            {
                return Left.Rect.ActorIndex < Right.Rect.ActorIndex;
            }
            return Left.ScreenAreaPixels > Right.ScreenAreaPixels;
        });

        for (uint32 Index : OccluderOrder)
        {
            const FProjectedCandidate& Candidate = ProjectedCandidates[Index];
            if (Grid.RasterizeConservative(Candidate.Rect))
            {
                ++OccluderCount;
                bHasOccluderDepth = true;
                bDepthDirty = true;
            }
        }
    }

    LastStats.OccluderCount = OccluderCount;
    FinishStats();
}
