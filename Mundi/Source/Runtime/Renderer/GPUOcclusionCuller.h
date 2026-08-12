#pragma once

#include "Vector.h"

#include <array>
#include <d3d11.h>

class D3D11RHI;
class FBVHierarchy;
class FSceneView;
class UMeshComponent;
class UShader;

struct FGPUOcclusionStats
{
	uint32 CandidateCount = 0;
	uint32 TestedCount = 0;
	uint32 CulledCount = 0;
	uint32 HZBMipCount = 0;
	uint32 ResultLatencyFrames = 0;
	uint64 DispatchCount = 0;
	float CPUTimeMs = 0.0f;
	float GPUTimeMs = 0.0f;
	bool bResultAvailable = false;
};

/**
 * Previous-frame GPU HZB occlusion culler.
 *
 * GPU visibility is copied into a three-slot staging ring and is mapped with
 * D3D11_MAP_FLAG_DO_NOT_WAIT starting on the following view frame. Small camera
 * motion reuses temporally conservative results; camera cuts fail open.
 * Rendering remains one DrawIndexed call per visible static-mesh component.
 */
class FGPUOcclusionCuller
{
public:
	FGPUOcclusionCuller();
	~FGPUOcclusionCuller();

	void Initialize(D3D11RHI* InRHI);
	void Release();
	void SetEnabled(bool bInEnabled);

	void PrepareCandidatesAndCull(
		TArray<UMeshComponent*>& InOutMeshes,
		const FBVHierarchy* BVH,
		const FSceneView& View,
		uint64 SpatialRevision);

	bool Submit(
		ID3D11ShaderResourceView* SceneDepthSRV,
		const FSceneView& View,
		uint64 SpatialRevision);

	const FGPUOcclusionStats& GetStats() const { return Stats; }

private:
	struct FGPUCandidate
	{
		FVector BoundsMin;
		uint32 ComponentIndex = 0;
		FVector BoundsMax;
		uint32 Padding = 0;
	};

	struct FViewSnapshot
	{
		FMatrix ViewProjection;
		FVector ViewLocation;
		FQuat ViewRotation;
		uint64 SpatialRevision = 0;
		uint32 ViewportStartX = 0;
		uint32 ViewportStartY = 0;
		uint32 ViewportWidth = 0;
		uint32 ViewportHeight = 0;
		float NearClip = 0.0f;
		float FarClip = 0.0f;
		float FieldOfView = 0.0f;
		float ZoomFactor = 0.0f;
		uint32 ProjectionMode = 0;
		bool bValid = false;
	};

	struct FReadbackSlot
	{
		ID3D11Buffer* StagingBuffer = nullptr;
		TArray<uint32> ComponentIndices;
		FViewSnapshot Snapshot;
		uint64 SubmittedFrame = 0;
		uint64 Generation = 0;
		uint32 CandidateCount = 0;
		bool bPending = false;
	};

	struct alignas(16) FHZBInitConstants
	{
		uint32 ViewportStartX = 0;
		uint32 ViewportStartY = 0;
		uint32 ViewportWidth = 0;
		uint32 ViewportHeight = 0;
	};

	struct alignas(16) FHZBReduceConstants
	{
		uint32 SourceWidth = 0;
		uint32 SourceHeight = 0;
		uint32 DestinationWidth = 0;
		uint32 DestinationHeight = 0;
	};

	struct alignas(16) FCullConstants
	{
		FMatrix ViewProjection;
		FMatrix PreviousViewProjection;
		uint32 ViewportWidth = 0;
		uint32 ViewportHeight = 0;
		uint32 CandidateCount = 0;
		uint32 HZBMipCount = 0;
		float NearClip = 0.0f;
		float FarClip = 0.0f;
		float WorldDepthBias = 0.05f;
		float MotionPaddingScale = 0.0f;
		uint32 bPerspectiveProjection = 1;
		uint32 bUseTemporalMotion = 0;
		float Padding0 = 0.0f;
		float Padding1 = 0.0f;
	};

	static_assert(sizeof(FGPUCandidate) == 32, "GPU occlusion candidate layout must match HLSL");
	static_assert(sizeof(FHZBInitConstants) % 16 == 0, "HZB constants must be 16-byte aligned");
	static_assert(sizeof(FHZBReduceConstants) % 16 == 0, "HZB constants must be 16-byte aligned");
	static_assert(sizeof(FCullConstants) % 16 == 0, "Occlusion constants must be 16-byte aligned");

	static FViewSnapshot MakeViewSnapshot(const FSceneView& View, uint64 SpatialRevision);
	static bool HaveMatchingSceneAndProjection(const FViewSnapshot& A, const FViewSnapshot& B);
	static bool IsCameraCut(const FViewSnapshot& A, const FViewSnapshot& B);
	static bool AreViewsCompatible(const FViewSnapshot& A, const FViewSnapshot& B);

	bool EnsureCandidateCapacity(uint32 RequiredCount);
	bool EnsureHZBResources(uint32 Width, uint32 Height);
	bool UpdateConstantBuffer(ID3D11Buffer* Buffer, const void* Data, uint32 DataSize);
	void PollReadbacks(const FViewSnapshot& CurrentView);
	void ConsumeReadback(FReadbackSlot& Slot, const uint32* VisibilityData, const FViewSnapshot& CurrentView);
	void ResetVisibilityState();
	void ReleaseCandidateResources();
	void ReleaseHZBResources();
	void ReleaseReadbackResources();

private:
	static constexpr uint32 ReadbackSlotCount = 3;
	static constexpr uint32 MinimumReadbackLatency = 1;
	static constexpr uint32 MaximumTemporalResultAge = 3;
	static constexpr uint8 OccludedFrameThreshold = 2;
	static constexpr float MaximumTemporalTranslation = 5.0f;
	// Quaternion dot = cos(full rotation angle / 2): approximately 12 degrees.
	static constexpr float MinimumTemporalRotationDot = 0.9945219f;
	static constexpr float BaseWorldDepthBias = 0.05f;
	static constexpr float MotionDepthBiasScale = 1.25f;
	static constexpr float MotionScreenPaddingScale = 1.5f;

	D3D11RHI* RHI = nullptr;
	UShader* HZBInitShader = nullptr;
	UShader* HZBReduceShader = nullptr;
	UShader* OcclusionCullShader = nullptr;

	ID3D11Buffer* HZBInitConstantBuffer = nullptr;
	ID3D11Buffer* HZBReduceConstantBuffer = nullptr;
	ID3D11Buffer* CullConstantBuffer = nullptr;

	ID3D11Buffer* CandidateBuffer = nullptr;
	ID3D11ShaderResourceView* CandidateBufferSRV = nullptr;
	ID3D11Buffer* VisibilityBuffer = nullptr;
	ID3D11ShaderResourceView* VisibilityBufferSRV = nullptr;
	ID3D11UnorderedAccessView* VisibilityBufferUAV = nullptr;
	uint32 CandidateCapacity = 0;

	ID3D11Texture2D* HZBTexture = nullptr;
	ID3D11ShaderResourceView* HZBAllMipsSRV = nullptr;
	TArray<ID3D11ShaderResourceView*> HZBMipSRVs;
	TArray<ID3D11UnorderedAccessView*> HZBMipUAVs;
	uint32 HZBWidth = 0;
	uint32 HZBHeight = 0;
	uint32 HZBMipCount = 0;

	std::array<FReadbackSlot, ReadbackSlotCount> ReadbackSlots;
	TArray<FGPUCandidate> CurrentCandidates;
	TArray<uint32> CurrentCandidateIndices;
	FViewSnapshot CurrentViewSnapshot;
	FViewSnapshot VisibilityViewSnapshot;
	FViewSnapshot LastSubmittedViewSnapshot;
	TArray<uint8> OccludedStreak;
	TArray<uint8> LastVisibleState;
	TArray<FAABB> CachedBoundsByComponentIndex;
	TArray<uint8> CachedBoundsValid;
	uint64 BoundsCacheSpatialRevision = 0;

	FGPUOcclusionStats Stats;
	uint64 ViewFrameNumber = 0;
	uint64 VisibilityResultFrame = 0;
	uint64 Generation = 1;
	bool bEnabled = false;
};
