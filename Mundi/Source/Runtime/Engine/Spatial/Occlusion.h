#pragma once

struct FVector;
struct FVector4;
struct FMatrix;
struct FAABB;

/**
 * Frustum culling을 통과한 정적 메시의 CPU occlusion 입력입니다.
 * Bound는 이미 월드 공간이므로 WorldViewProj/WorldView에는 각각 ViewProjection/View만 들어갑니다.
 */
struct FCandidateDrawable
{
    uint32 ActorIndex = 0;
    FAABB Bound;
    FMatrix WorldViewProj;
    FMatrix WorldView;
    float NearClip = 0.1f;
    float FarClip = 1000.0f;
    bool bCanOcclude = false;
};

struct FOcclusionRect
{
    float MinX = 0.0f;
    float MinY = 0.0f;
    float MaxX = 0.0f;
    float MaxY = 0.0f;
    float MinZ = 1.0f; // AABB에서 카메라와 가장 가까운 선형 깊이
    float MaxZ = 1.0f; // AABB에서 카메라와 가장 먼 선형 깊이
    uint32 ActorIndex = 0;
};

struct FOcclusionCullingStats
{
    uint32 RegisteredMeshCount = 0;
    uint32 FrustumVisibleCount = 0;
    uint32 FrustumCulledCount = 0;
    uint32 CandidateCount = 0;
    uint32 ProjectedCount = 0;
    uint32 OccluderCount = 0;
    uint32 CulledCount = 0;
    uint32 FinalVisibleCount = 0;
    uint32 OpaqueDrawCallCount = 0;
    uint32 ShaderChangeCount = 0;
    uint32 MaterialBindCount = 0;
    uint32 BufferChangeCount = 0;
    float CPUTimeMs = 0.0f;
    float MaterialSortCPUTimeMs = 0.0f;
    bool bFrustumEnabled = false;
    bool bOcclusionEnabled = false;
	bool bGPUOcclusion = false;
	bool bGPUResultAvailable = false;
	uint32 GPUHZBMipCount = 0;
	uint32 GPUResultLatencyFrames = 0;
	uint64 GPUDispatchCount = 0;
	float GPUTimeMs = 0.0f;
    bool bMaterialSortingEnabled = false;
	bool bStaticMeshCachedPathEnabled = false;
	uint32 StaticMeshCachedCommandCount = 0;
	uint32 StaticMeshCachedComponentCount = 0;
	uint64 StaticMeshCacheRebuildCount = 0;
	float StaticMeshCacheLastRebuildTimeMs = 0.0f;
};

/**
 * 저해상도 CPU depth + MAX HZB입니다.
 *
 * 깊이는 0(near)~1(far)이며, level 0에는 각 셀을 완전히 덮는 오클루더의 가장 가까운
 * "가장 먼 깊이"가 저장됩니다. 상위 레벨은 MAX로 축약하므로, HZB 셀 하나라도 덮이지
 * 않았으면 1.0이 전파됩니다. 따라서 HZBMax < CandidateMinZ일 때만 보수적으로 가립니다.
 */
class FOcclusionGrid
{
public:
    void Initialize(int InWidth, int InHeight);
    void Clear();

    /** 오클루더 사각형 안에 완전히 포함되는 셀만 기록합니다. */
    bool RasterizeConservative(const FOcclusionRect& Rect, float ErodeScale = 0.6f);

    /** 현재 level 0에서 HZB를 재구축합니다. */
    void BuildHZB();

    /** 사각형이 HZB에서 완전히 가려졌는지 모든 관련 HZB 셀을 검사합니다. */
    bool IsRectOccluded(const FOcclusionRect& Rect, float DepthBias) const;

    bool CanRasterizeConservative(const FOcclusionRect& Rect, float ErodeScale = 0.6f) const;

    int GetWidth() const { return Width; }
    int GetHeight() const { return Height; }

private:
    bool ComputeConservativeRasterBounds(
        const FOcclusionRect& Rect,
        float ErodeScale,
        int& OutMinX,
        int& OutMinY,
        int& OutMaxX,
        int& OutMaxY) const;

private:
    int Width = 0;
    int Height = 0;
    TArray<float> Depth;
    TArray<TArray<float>> BuildLevels;
    TArray<int> LevelWidths;
    TArray<int> LevelHeights;
    bool bHZBValid = false;
};

/**
 * CPU occlusion manager.
 *
 * 모든 후보를 두 번 투영하고 모든 AABB 사각형을 먼저 채우던 기존 방식 대신:
 *  1. 후보를 한 번만 투영
 *  2. 가까운 순서로 정렬
 *  3. 작은 배치 단위로 HZB 검사
 *  4. 실제로 보이는 큰 불투명 후보만 다음 배치의 오클루더로 등록
 * 합니다.
 */
class FOcclusionCullingManagerCPU
{
public:
    void Initialize(int GridW, int GridH) { Grid.Initialize(GridW, GridH); }
    void Shutdown() {}

    void CullFrontToBack(
        const TArray<FCandidateDrawable>& Candidates,
        int ViewW,
        int ViewH,
        TArray<uint8_t>& OutVisibleFlags);

    void BeginFrameStats(
        uint32 RegisteredMeshCount,
        uint32 FrustumVisibleCount,
        bool bFrustumEnabled,
        bool bOcclusionEnabled,
        bool bMaterialSortingEnabled);
    void SetFinalVisibleCount(uint32 Count) { LastStats.FinalVisibleCount = Count; }
    void SetMaterialSortCPUTime(float TimeMs) { LastStats.MaterialSortCPUTimeMs = TimeMs; }
    void SetOpaqueDrawStats(uint32 DrawCalls, uint32 ShaderChanges, uint32 MaterialBinds, uint32 BufferChanges)
    {
        LastStats.OpaqueDrawCallCount = DrawCalls;
        LastStats.ShaderChangeCount = ShaderChanges;
        LastStats.MaterialBindCount = MaterialBinds;
        LastStats.BufferChangeCount = BufferChanges;
    }
	void AccumulateOpaqueDrawStats(uint32 DrawCalls, uint32 ShaderChanges, uint32 MaterialBinds, uint32 BufferChanges)
	{
		LastStats.OpaqueDrawCallCount += DrawCalls;
		LastStats.ShaderChangeCount += ShaderChanges;
		LastStats.MaterialBindCount += MaterialBinds;
		LastStats.BufferChangeCount += BufferChanges;
	}
	void SetStaticMeshCacheStats(
		bool bEnabled,
		uint32 CommandCount,
		uint32 ComponentCount,
		uint64 RebuildCount,
		float LastRebuildTimeMs)
	{
		LastStats.bStaticMeshCachedPathEnabled = bEnabled;
		LastStats.StaticMeshCachedCommandCount = CommandCount;
		LastStats.StaticMeshCachedComponentCount = ComponentCount;
		LastStats.StaticMeshCacheRebuildCount = RebuildCount;
		LastStats.StaticMeshCacheLastRebuildTimeMs = LastRebuildTimeMs;
	}
	void SetGPUOcclusionStats(
		uint32 CandidateCount,
		uint32 TestedCount,
		uint32 CulledCount,
		uint32 HZBMipCount,
		uint32 ResultLatencyFrames,
		uint64 DispatchCount,
		float CPUTimeMs,
		float GPUTimeMs,
		bool bResultAvailable)
	{
		LastStats.bGPUOcclusion = true;
		LastStats.bGPUResultAvailable = bResultAvailable;
		LastStats.CandidateCount = CandidateCount;
		LastStats.ProjectedCount = TestedCount;
		LastStats.OccluderCount = 0;
		LastStats.CulledCount = CulledCount;
		LastStats.GPUHZBMipCount = HZBMipCount;
		LastStats.GPUResultLatencyFrames = ResultLatencyFrames;
		LastStats.GPUDispatchCount = DispatchCount;
		LastStats.CPUTimeMs = CPUTimeMs;
		LastStats.GPUTimeMs = GPUTimeMs;
	}

    const FOcclusionGrid& GetGrid() const { return Grid; }
    const FOcclusionCullingStats& GetLastStats() const { return LastStats; }
    void ResetStats() { LastStats = {}; }

private:
    struct FProjectedCandidate
    {
        FOcclusionRect Rect;
        float ScreenAreaPixels = 0.0f;
        bool bCanOcclude = false;
        bool bRawOccluded = false;
    };

    static bool ComputeRectAndMinZ(
        const FCandidateDrawable& Candidate,
        int ViewW,
        int ViewH,
        FOcclusionRect& OutRect);

    static inline void MulPointRow(const float In[4], const FMatrix& Matrix, float Out[4])
    {
        Out[0] = In[0] * Matrix.M[0][0] + In[1] * Matrix.M[1][0] + In[2] * Matrix.M[2][0] + In[3] * Matrix.M[3][0];
        Out[1] = In[0] * Matrix.M[0][1] + In[1] * Matrix.M[1][1] + In[2] * Matrix.M[2][1] + In[3] * Matrix.M[3][1];
        Out[2] = In[0] * Matrix.M[0][2] + In[1] * Matrix.M[1][2] + In[2] * Matrix.M[2][2] + In[3] * Matrix.M[3][2];
        Out[3] = In[0] * Matrix.M[0][3] + In[1] * Matrix.M[1][3] + In[2] * Matrix.M[2][3] + In[3] * Matrix.M[3][3];
    }

    bool ApplyOccludedHysteresis(uint32 ActorIndex, bool bRawOccluded);
    void MarkVisible(uint32 ActorIndex);

private:
    static constexpr uint32 CandidateBatchSize = 256;
    static constexpr uint8 OccludedFrameThreshold = 2;

    FOcclusionGrid Grid;
    TArray<uint8_t> OccludedStreak;
    TArray<uint8_t> LastState; // 0=occluded, 1=visible
    FOcclusionCullingStats LastStats;
};
