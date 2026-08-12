#include "pch.h"
#include "GPUOcclusionCuller.h"

#include "BVHierarchy.h"
#include "D3D11RHI.h"
#include "MeshComponent.h"
#include "GPUProfile.h"
#include "ResourceManager.h"
#include "SceneView.h"
#include "Shader.h"
#include "StaticMeshComponent.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace
{
	template <typename T>
	void SafeRelease(T*& Resource)
	{
		if (Resource)
		{
			Resource->Release();
			Resource = nullptr;
		}
	}

	uint32 CalculateMipCount(uint32 Width, uint32 Height)
	{
		uint32 MipCount = 1;
		while (Width > 1 || Height > 1)
		{
			Width = std::max<uint32>(1, Width / 2);
			Height = std::max<uint32>(1, Height / 2);
			++MipCount;
		}
		return MipCount;
	}

	uint32 GrowCapacity(uint32 CurrentCapacity, uint32 RequiredCapacity)
	{
		uint32 NewCapacity = std::max<uint32>(CurrentCapacity, 256);
		while (NewCapacity < RequiredCapacity)
		{
			const uint32 Doubled = NewCapacity <= UINT32_MAX / 2 ? NewCapacity * 2 : RequiredCapacity;
			NewCapacity = std::max(Doubled, RequiredCapacity);
		}
		return NewCapacity;
	}
}

FGPUOcclusionCuller::FGPUOcclusionCuller() = default;

FGPUOcclusionCuller::~FGPUOcclusionCuller()
{
	Release();
}

void FGPUOcclusionCuller::Initialize(D3D11RHI* InRHI)
{
	if (RHI && RHI != InRHI)
	{
		Release();
	}

	RHI = InRHI;
	if (!RHI)
	{
		return;
	}

	auto CreateDynamicConstantBuffer = [this](uint32 Size, ID3D11Buffer** OutBuffer)
	{
		if (*OutBuffer)
		{
			return true;
		}
		D3D11_BUFFER_DESC Desc{};
		Desc.Usage = D3D11_USAGE_DYNAMIC;
		Desc.ByteWidth = Size;
		Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		return SUCCEEDED(RHI->GetDevice()->CreateBuffer(&Desc, nullptr, OutBuffer));
	};

	if (!CreateDynamicConstantBuffer(sizeof(FHZBInitConstants), &HZBInitConstantBuffer) ||
		!CreateDynamicConstantBuffer(sizeof(FHZBReduceConstants), &HZBReduceConstantBuffer) ||
		!CreateDynamicConstantBuffer(sizeof(FCullConstants), &CullConstantBuffer))
	{
		UE_LOG("GPU Occlusion: constant buffer creation failed.\n");
	}

	if (!HZBInitShader)
	{
		HZBInitShader = UResourceManager::GetInstance().Load<UShader>("Shaders/Occlusion/OcclusionHZBInit_CS.hlsl");
	}
	if (!HZBReduceShader)
	{
		HZBReduceShader = UResourceManager::GetInstance().Load<UShader>("Shaders/Occlusion/OcclusionHZBReduce_CS.hlsl");
	}
	if (!OcclusionCullShader)
	{
		OcclusionCullShader = UResourceManager::GetInstance().Load<UShader>("Shaders/Occlusion/OcclusionCull_CS.hlsl");
	}
}

void FGPUOcclusionCuller::SetEnabled(bool bInEnabled)
{
	if (bEnabled == bInEnabled)
	{
		return;
	}

	bEnabled = bInEnabled;
	++Generation;
	ResetVisibilityState();
	CurrentCandidates.Empty();
	CurrentCandidateIndices.Empty();
	CurrentViewSnapshot = {};
	Stats = {};
}

FGPUOcclusionCuller::FViewSnapshot FGPUOcclusionCuller::MakeViewSnapshot(
	const FSceneView& View,
	uint64 SpatialRevision)
{
	FViewSnapshot Snapshot{};
	Snapshot.ViewProjection = View.ViewMatrix * View.ProjectionMatrix;
	Snapshot.ViewLocation = View.ViewLocation;
	Snapshot.ViewRotation = View.ViewRotation;
	Snapshot.SpatialRevision = SpatialRevision;
	Snapshot.ViewportStartX = View.ViewRect.MinX;
	Snapshot.ViewportStartY = View.ViewRect.MinY;
	Snapshot.ViewportWidth = View.ViewRect.Width();
	Snapshot.ViewportHeight = View.ViewRect.Height();
	Snapshot.NearClip = View.NearClip;
	Snapshot.FarClip = View.FarClip;
	Snapshot.FieldOfView = View.FieldOfView;
	Snapshot.ZoomFactor = View.ZoomFactor;
	Snapshot.ProjectionMode = static_cast<uint32>(View.ProjectionMode);
	Snapshot.bValid = Snapshot.ViewportWidth > 0 && Snapshot.ViewportHeight > 0;
	return Snapshot;
}

bool FGPUOcclusionCuller::HaveMatchingSceneAndProjection(const FViewSnapshot& A, const FViewSnapshot& B)
{
	if (!A.bValid || !B.bValid || A.SpatialRevision != B.SpatialRevision ||
		A.ViewportStartX != B.ViewportStartX || A.ViewportStartY != B.ViewportStartY ||
		A.ViewportWidth != B.ViewportWidth || A.ViewportHeight != B.ViewportHeight ||
		A.ProjectionMode != B.ProjectionMode)
	{
		return false;
	}

	return std::fabs(A.NearClip - B.NearClip) <= 1.0e-4f &&
		std::fabs(A.FarClip - B.FarClip) <= 1.0e-3f &&
		std::fabs(A.FieldOfView - B.FieldOfView) <= 1.0e-3f &&
		std::fabs(A.ZoomFactor - B.ZoomFactor) <= 1.0e-4f;
}

bool FGPUOcclusionCuller::IsCameraCut(const FViewSnapshot& A, const FViewSnapshot& B)
{
	if (!A.bValid || !B.bValid)
	{
		return true;
	}

	const float TranslationLimitSquared = MaximumTemporalTranslation * MaximumTemporalTranslation;
	if ((A.ViewLocation - B.ViewLocation).SizeSquared() > TranslationLimitSquared)
	{
		return true;
	}

	const float RotationDot = std::fabs(FQuat::Dot(A.ViewRotation, B.ViewRotation));
	return RotationDot < MinimumTemporalRotationDot;
}

bool FGPUOcclusionCuller::AreViewsCompatible(const FViewSnapshot& A, const FViewSnapshot& B)
{
	return HaveMatchingSceneAndProjection(A, B) && !IsCameraCut(A, B);
}

void FGPUOcclusionCuller::PrepareCandidatesAndCull(
	TArray<UMeshComponent*>& InOutMeshes,
	const FBVHierarchy* BVH,
	const FSceneView& View,
	uint64 SpatialRevision)
{
	const auto StartTime = std::chrono::steady_clock::now();
	++ViewFrameNumber;

	Stats.CandidateCount = 0;
	Stats.CulledCount = 0;
	Stats.CPUTimeMs = 0.0f;
	Stats.GPUTimeMs = static_cast<float>(FGPUProfiler::GetInstance().GetStat("GPU_Occlusion"));
	CurrentCandidates.Empty();
	CurrentCandidateIndices.Empty();
	CurrentViewSnapshot = MakeViewSnapshot(View, SpatialRevision);

	if (!bEnabled || !RHI || !CurrentViewSnapshot.bValid)
	{
		ResetVisibilityState();
		return;
	}

	PollReadbacks(CurrentViewSnapshot);
	const bool bTemporalResultExpired = VisibilityViewSnapshot.bValid &&
		ViewFrameNumber > VisibilityResultFrame + MaximumTemporalResultAge;
	if (VisibilityViewSnapshot.bValid &&
		(!AreViewsCompatible(VisibilityViewSnapshot, CurrentViewSnapshot) || bTemporalResultExpired))
	{
		ResetVisibilityState();
	}
	if (SpatialRevision == 0 || BoundsCacheSpatialRevision != SpatialRevision)
	{
		CachedBoundsByComponentIndex.clear();
		CachedBoundsValid.clear();
		BoundsCacheSpatialRevision = SpatialRevision;
	}

	CurrentCandidates.Reserve(InOutMeshes.Num());
	CurrentCandidateIndices.Reserve(InOutMeshes.Num());
	for (UMeshComponent* MeshComponent : InOutMeshes)
	{
		UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(MeshComponent);
		if (!StaticMeshComponent || StaticMeshComponent->InternalIndex == UINT32_MAX)
		{
			continue;
		}

		const uint32 ComponentIndex = StaticMeshComponent->InternalIndex;
		FAABB Bounds;
		bool bHasCachedBounds = SpatialRevision != 0 &&
			ComponentIndex < static_cast<uint32>(CachedBoundsValid.Num()) &&
			CachedBoundsValid[ComponentIndex] != 0;
		if (bHasCachedBounds)
		{
			Bounds = CachedBoundsByComponentIndex[ComponentIndex];
		}
		else
		{
			if (!BVH || !BVH->GetCachedBounds(StaticMeshComponent, Bounds))
			{
				Bounds = StaticMeshComponent->GetWorldAABB();
			}
			if (SpatialRevision != 0)
			{
				if (static_cast<uint32>(CachedBoundsValid.Num()) <= ComponentIndex)
				{
					CachedBoundsByComponentIndex.resize(static_cast<size_t>(ComponentIndex) + 1);
					CachedBoundsValid.resize(static_cast<size_t>(ComponentIndex) + 1, 0);
				}
				CachedBoundsByComponentIndex[ComponentIndex] = Bounds;
				CachedBoundsValid[ComponentIndex] = 1;
			}
		}

		FGPUCandidate Candidate{};
		Candidate.BoundsMin = Bounds.Min;
		Candidate.BoundsMax = Bounds.Max;
		Candidate.ComponentIndex = ComponentIndex;
		CurrentCandidates.Add(Candidate);
		CurrentCandidateIndices.Add(Candidate.ComponentIndex);
	}
	Stats.CandidateCount = static_cast<uint32>(CurrentCandidates.Num());

	if (VisibilityViewSnapshot.bValid && !LastVisibleState.IsEmpty())
	{
		TArray<UMeshComponent*> VisibleMeshes;
		VisibleMeshes.Reserve(InOutMeshes.Num());
		for (UMeshComponent* MeshComponent : InOutMeshes)
		{
			UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(MeshComponent);
			const uint32 ComponentIndex = StaticMeshComponent ? StaticMeshComponent->InternalIndex : UINT32_MAX;
			const bool bOccluded = ComponentIndex < static_cast<uint32>(LastVisibleState.Num()) &&
				LastVisibleState[ComponentIndex] == 0;
			if (!bOccluded)
			{
				VisibleMeshes.Add(MeshComponent);
			}
			else
			{
				++Stats.CulledCount;
			}
		}
		InOutMeshes = std::move(VisibleMeshes);
	}

	const auto EndTime = std::chrono::steady_clock::now();
	Stats.CPUTimeMs += std::chrono::duration<float, std::milli>(EndTime - StartTime).count();
}

bool FGPUOcclusionCuller::EnsureCandidateCapacity(uint32 RequiredCount)
{
	if (RequiredCount == 0)
	{
		return false;
	}
	if (RequiredCount <= CandidateCapacity && CandidateBuffer && CandidateBufferSRV &&
		VisibilityBuffer && VisibilityBufferUAV)
	{
		return true;
	}

	const uint32 NewCapacity = GrowCapacity(CandidateCapacity, RequiredCount);
	ReleaseCandidateResources();
	ReleaseReadbackResources();

	if (FAILED(RHI->CreateStructuredBuffer(sizeof(FGPUCandidate), NewCapacity, nullptr, &CandidateBuffer)) ||
		FAILED(RHI->CreateStructuredBufferSRV(CandidateBuffer, &CandidateBufferSRV)) ||
		FAILED(RHI->CreateGPUWritableStructuredBuffer(
			sizeof(uint32), NewCapacity, &VisibilityBuffer, &VisibilityBufferSRV, &VisibilityBufferUAV)))
	{
		UE_LOG("GPU Occlusion: candidate/visibility buffer creation failed.\n");
		ReleaseCandidateResources();
		return false;
	}

	for (FReadbackSlot& Slot : ReadbackSlots)
	{
		D3D11_BUFFER_DESC Desc{};
		Desc.Usage = D3D11_USAGE_STAGING;
		Desc.ByteWidth = sizeof(uint32) * NewCapacity;
		Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		Desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		Desc.StructureByteStride = sizeof(uint32);
		if (FAILED(RHI->GetDevice()->CreateBuffer(&Desc, nullptr, &Slot.StagingBuffer)))
		{
			UE_LOG("GPU Occlusion: staging readback buffer creation failed.\n");
			ReleaseCandidateResources();
			ReleaseReadbackResources();
			return false;
		}
	}

	CandidateCapacity = NewCapacity;
	return true;
}

bool FGPUOcclusionCuller::EnsureHZBResources(uint32 Width, uint32 Height)
{
	if (Width == 0 || Height == 0)
	{
		return false;
	}
	if (HZBTexture && HZBWidth == Width && HZBHeight == Height)
	{
		return true;
	}

	ReleaseHZBResources();
	HZBWidth = Width;
	HZBHeight = Height;
	HZBMipCount = CalculateMipCount(Width, Height);

	D3D11_TEXTURE2D_DESC TextureDesc{};
	TextureDesc.Width = Width;
	TextureDesc.Height = Height;
	TextureDesc.MipLevels = HZBMipCount;
	TextureDesc.ArraySize = 1;
	TextureDesc.Format = DXGI_FORMAT_R32_FLOAT;
	TextureDesc.SampleDesc.Count = 1;
	TextureDesc.Usage = D3D11_USAGE_DEFAULT;
	TextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	if (FAILED(RHI->GetDevice()->CreateTexture2D(&TextureDesc, nullptr, &HZBTexture)))
	{
		ReleaseHZBResources();
		return false;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC AllMipsDesc{};
	AllMipsDesc.Format = DXGI_FORMAT_R32_FLOAT;
	AllMipsDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	AllMipsDesc.Texture2D.MostDetailedMip = 0;
	AllMipsDesc.Texture2D.MipLevels = HZBMipCount;
	if (FAILED(RHI->GetDevice()->CreateShaderResourceView(HZBTexture, &AllMipsDesc, &HZBAllMipsSRV)))
	{
		ReleaseHZBResources();
		return false;
	}

	HZBMipSRVs.resize(HZBMipCount, nullptr);
	HZBMipUAVs.resize(HZBMipCount, nullptr);
	for (uint32 MipIndex = 0; MipIndex < HZBMipCount; ++MipIndex)
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc{};
		SRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
		SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		SRVDesc.Texture2D.MostDetailedMip = MipIndex;
		SRVDesc.Texture2D.MipLevels = 1;
		if (FAILED(RHI->GetDevice()->CreateShaderResourceView(HZBTexture, &SRVDesc, &HZBMipSRVs[MipIndex])))
		{
			ReleaseHZBResources();
			return false;
		}

		D3D11_UNORDERED_ACCESS_VIEW_DESC UAVDesc{};
		UAVDesc.Format = DXGI_FORMAT_R32_FLOAT;
		UAVDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		UAVDesc.Texture2D.MipSlice = MipIndex;
		if (FAILED(RHI->GetDevice()->CreateUnorderedAccessView(HZBTexture, &UAVDesc, &HZBMipUAVs[MipIndex])))
		{
			ReleaseHZBResources();
			return false;
		}
	}

	return true;
}

bool FGPUOcclusionCuller::UpdateConstantBuffer(ID3D11Buffer* Buffer, const void* Data, uint32 DataSize)
{
	if (!Buffer || !Data)
	{
		return false;
	}
	D3D11_MAPPED_SUBRESOURCE Mapped{};
	if (FAILED(RHI->GetDeviceContext()->Map(Buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		return false;
	}
	memcpy(Mapped.pData, Data, DataSize);
	RHI->GetDeviceContext()->Unmap(Buffer, 0);
	return true;
}

bool FGPUOcclusionCuller::Submit(
	ID3D11ShaderResourceView* SceneDepthSRV,
	const FSceneView& View,
	uint64 SpatialRevision)
{
	const auto StartTime = std::chrono::steady_clock::now();
	if (!bEnabled || !RHI || !SceneDepthSRV || CurrentCandidates.IsEmpty() ||
		!HZBInitConstantBuffer || !HZBReduceConstantBuffer || !CullConstantBuffer ||
		!HZBInitShader || !HZBReduceShader || !OcclusionCullShader)
	{
		return false;
	}

	const FViewSnapshot SubmitView = MakeViewSnapshot(View, SpatialRevision);
	if (!AreViewsCompatible(CurrentViewSnapshot, SubmitView))
	{
		return false;
	}

	ID3D11ComputeShader* InitCS = HZBInitShader->GetComputeShader();
	ID3D11ComputeShader* ReduceCS = HZBReduceShader->GetComputeShader();
	ID3D11ComputeShader* CullCS = OcclusionCullShader->GetComputeShader();
	if (!InitCS || !ReduceCS || !CullCS ||
		!EnsureCandidateCapacity(static_cast<uint32>(CurrentCandidates.Num())) ||
		!EnsureHZBResources(SubmitView.ViewportWidth, SubmitView.ViewportHeight))
	{
		return false;
	}

	FReadbackSlot* ReadbackSlot = nullptr;
	for (uint32 Offset = 0; Offset < ReadbackSlotCount; ++Offset)
	{
		FReadbackSlot& Slot = ReadbackSlots[(ViewFrameNumber + Offset) % ReadbackSlotCount];
		if (!Slot.bPending)
		{
			ReadbackSlot = &Slot;
			break;
		}
	}
	if (!ReadbackSlot)
	{
		return false;
	}

	RHI->UpdateStructuredBuffer(
		CandidateBuffer,
		CurrentCandidates.data(),
		static_cast<UINT>(CurrentCandidates.Num() * sizeof(FGPUCandidate)));

	ID3D11DeviceContext* Context = RHI->GetDeviceContext();
	GPU_TIME_PROFILE("GPU_Occlusion")
	RHI->OMSetCustomRenderTargets(0, nullptr, nullptr);

	FHZBInitConstants InitConstants{};
	InitConstants.ViewportStartX = SubmitView.ViewportStartX;
	InitConstants.ViewportStartY = SubmitView.ViewportStartY;
	InitConstants.ViewportWidth = SubmitView.ViewportWidth;
	InitConstants.ViewportHeight = SubmitView.ViewportHeight;
	if (!UpdateConstantBuffer(HZBInitConstantBuffer, &InitConstants, sizeof(InitConstants)))
	{
		return false;
	}
	RHI->CSSetShader(InitCS);
	RHI->CSSetConstantBuffers(0, 1, &HZBInitConstantBuffer);
	RHI->CSSetShaderResources(0, 1, &SceneDepthSRV);
	RHI->CSSetUnorderedAccessViews(0, 1, &HZBMipUAVs[0]);
	RHI->Dispatch((SubmitView.ViewportWidth + 7) / 8, (SubmitView.ViewportHeight + 7) / 8, 1);
	RHI->UnbindComputeResources();

	uint32 SourceWidth = SubmitView.ViewportWidth;
	uint32 SourceHeight = SubmitView.ViewportHeight;
	for (uint32 MipIndex = 1; MipIndex < HZBMipCount; ++MipIndex)
	{
		const uint32 DestinationWidth = std::max<uint32>(1, SourceWidth / 2);
		const uint32 DestinationHeight = std::max<uint32>(1, SourceHeight / 2);
		FHZBReduceConstants ReduceConstants{};
		ReduceConstants.SourceWidth = SourceWidth;
		ReduceConstants.SourceHeight = SourceHeight;
		ReduceConstants.DestinationWidth = DestinationWidth;
		ReduceConstants.DestinationHeight = DestinationHeight;
		if (!UpdateConstantBuffer(HZBReduceConstantBuffer, &ReduceConstants, sizeof(ReduceConstants)))
		{
			return false;
		}
		ID3D11ShaderResourceView* SourceMipSRV = HZBMipSRVs[MipIndex - 1];
		ID3D11UnorderedAccessView* DestinationMipUAV = HZBMipUAVs[MipIndex];
		RHI->CSSetShader(ReduceCS);
		RHI->CSSetConstantBuffers(0, 1, &HZBReduceConstantBuffer);
		RHI->CSSetShaderResources(0, 1, &SourceMipSRV);
		RHI->CSSetUnorderedAccessViews(0, 1, &DestinationMipUAV);
		RHI->Dispatch((DestinationWidth + 7) / 8, (DestinationHeight + 7) / 8, 1);
		RHI->UnbindComputeResources();
		SourceWidth = DestinationWidth;
		SourceHeight = DestinationHeight;
	}

	FCullConstants CullConstants{};
	CullConstants.ViewProjection = SubmitView.ViewProjection;
	const bool bUseTemporalMotion = HaveMatchingSceneAndProjection(LastSubmittedViewSnapshot, SubmitView) &&
		!IsCameraCut(LastSubmittedViewSnapshot, SubmitView);
	CullConstants.PreviousViewProjection = bUseTemporalMotion
		? LastSubmittedViewSnapshot.ViewProjection
		: SubmitView.ViewProjection;
	CullConstants.ViewportWidth = SubmitView.ViewportWidth;
	CullConstants.ViewportHeight = SubmitView.ViewportHeight;
	CullConstants.CandidateCount = static_cast<uint32>(CurrentCandidates.Num());
	CullConstants.HZBMipCount = HZBMipCount;
	CullConstants.NearClip = SubmitView.NearClip;
	CullConstants.FarClip = SubmitView.FarClip;
	CullConstants.WorldDepthBias = BaseWorldDepthBias;
	CullConstants.MotionPaddingScale = bUseTemporalMotion ? MotionScreenPaddingScale : 0.0f;
	CullConstants.bPerspectiveProjection =
		SubmitView.ProjectionMode == static_cast<uint32>(ECameraProjectionMode::Perspective) ? 1u : 0u;
	CullConstants.bUseTemporalMotion = bUseTemporalMotion ? 1u : 0u;
	if (bUseTemporalMotion)
	{
		const float CameraTranslation = std::sqrt(
			(LastSubmittedViewSnapshot.ViewLocation - SubmitView.ViewLocation).SizeSquared());
		CullConstants.WorldDepthBias += CameraTranslation * MotionDepthBiasScale;
	}
	if (!UpdateConstantBuffer(CullConstantBuffer, &CullConstants, sizeof(CullConstants)))
	{
		return false;
	}
	ID3D11ShaderResourceView* CullSRVs[2] = { CandidateBufferSRV, HZBAllMipsSRV };
	RHI->CSSetShader(CullCS);
	RHI->CSSetConstantBuffers(0, 1, &CullConstantBuffer);
	RHI->CSSetShaderResources(0, 2, CullSRVs);
	RHI->CSSetUnorderedAccessViews(0, 1, &VisibilityBufferUAV);
	RHI->Dispatch((CullConstants.CandidateCount + 63) / 64, 1, 1);
	RHI->UnbindComputeResources();

	Context->CopyResource(ReadbackSlot->StagingBuffer, VisibilityBuffer);
	ReadbackSlot->ComponentIndices = CurrentCandidateIndices;
	ReadbackSlot->Snapshot = SubmitView;
	ReadbackSlot->SubmittedFrame = ViewFrameNumber;
	ReadbackSlot->Generation = Generation;
	ReadbackSlot->CandidateCount = CullConstants.CandidateCount;
	ReadbackSlot->bPending = true;
	LastSubmittedViewSnapshot = SubmitView;

	Stats.HZBMipCount = HZBMipCount;
	++Stats.DispatchCount;
	const auto EndTime = std::chrono::steady_clock::now();
	Stats.CPUTimeMs += std::chrono::duration<float, std::milli>(EndTime - StartTime).count();
	return true;
}

void FGPUOcclusionCuller::PollReadbacks(const FViewSnapshot& CurrentView)
{
	TArray<uint32> EligibleSlots;
	for (uint32 SlotIndex = 0; SlotIndex < ReadbackSlotCount; ++SlotIndex)
	{
		const FReadbackSlot& Slot = ReadbackSlots[SlotIndex];
		if (Slot.bPending && ViewFrameNumber >= Slot.SubmittedFrame + MinimumReadbackLatency)
		{
			EligibleSlots.Add(SlotIndex);
		}
	}
	std::sort(EligibleSlots.begin(), EligibleSlots.end(), [this](uint32 A, uint32 B)
	{
		return ReadbackSlots[A].SubmittedFrame < ReadbackSlots[B].SubmittedFrame;
	});

	for (uint32 SlotIndex : EligibleSlots)
	{
		FReadbackSlot& Slot = ReadbackSlots[SlotIndex];
		D3D11_MAPPED_SUBRESOURCE Mapped{};
		const HRESULT Result = RHI->GetDeviceContext()->Map(
			Slot.StagingBuffer,
			0,
			D3D11_MAP_READ,
			D3D11_MAP_FLAG_DO_NOT_WAIT,
			&Mapped);
		if (Result == DXGI_ERROR_WAS_STILL_DRAWING)
		{
			continue;
		}
		if (SUCCEEDED(Result))
		{
			ConsumeReadback(Slot, static_cast<const uint32*>(Mapped.pData), CurrentView);
			RHI->GetDeviceContext()->Unmap(Slot.StagingBuffer, 0);
		}
		Slot.bPending = false;
		Slot.ComponentIndices.Empty();
	}
}

void FGPUOcclusionCuller::ConsumeReadback(
	FReadbackSlot& Slot,
	const uint32* VisibilityData,
	const FViewSnapshot& CurrentView)
{
	if (!VisibilityData || Slot.Generation != Generation ||
		Slot.CandidateCount != static_cast<uint32>(Slot.ComponentIndices.Num()) ||
		!AreViewsCompatible(Slot.Snapshot, CurrentView))
	{
		return;
	}

	uint32 MaxComponentIndex = 0;
	for (uint32 ComponentIndex : Slot.ComponentIndices)
	{
		MaxComponentIndex = std::max(MaxComponentIndex, ComponentIndex);
	}
	if (static_cast<uint32>(OccludedStreak.Num()) <= MaxComponentIndex)
	{
		OccludedStreak.resize(static_cast<size_t>(MaxComponentIndex) + 1, 0);
		LastVisibleState.resize(static_cast<size_t>(MaxComponentIndex) + 1, 1);
	}

	for (uint32 CandidateIndex = 0; CandidateIndex < Slot.CandidateCount; ++CandidateIndex)
	{
		const uint32 ComponentIndex = Slot.ComponentIndices[CandidateIndex];
		if (VisibilityData[CandidateIndex] != 0)
		{
			OccludedStreak[ComponentIndex] = 0;
			LastVisibleState[ComponentIndex] = 1;
			continue;
		}

		if (OccludedStreak[ComponentIndex] < 255)
		{
			++OccludedStreak[ComponentIndex];
		}
		if (OccludedStreak[ComponentIndex] >= OccludedFrameThreshold)
		{
			LastVisibleState[ComponentIndex] = 0;
		}
	}

	VisibilityViewSnapshot = Slot.Snapshot;
	VisibilityResultFrame = Slot.SubmittedFrame;
	Stats.TestedCount = Slot.CandidateCount;
	Stats.ResultLatencyFrames = static_cast<uint32>(ViewFrameNumber - Slot.SubmittedFrame);
	Stats.bResultAvailable = true;
}

void FGPUOcclusionCuller::ResetVisibilityState()
{
	OccludedStreak.clear();
	LastVisibleState.clear();
	VisibilityViewSnapshot = {};
	LastSubmittedViewSnapshot = {};
	VisibilityResultFrame = 0;
	Stats.TestedCount = 0;
	Stats.ResultLatencyFrames = 0;
	Stats.bResultAvailable = false;
}

void FGPUOcclusionCuller::ReleaseCandidateResources()
{
	SafeRelease(VisibilityBufferUAV);
	SafeRelease(VisibilityBufferSRV);
	SafeRelease(VisibilityBuffer);
	SafeRelease(CandidateBufferSRV);
	SafeRelease(CandidateBuffer);
	CandidateCapacity = 0;
}

void FGPUOcclusionCuller::ReleaseHZBResources()
{
	for (ID3D11UnorderedAccessView*& UAV : HZBMipUAVs)
	{
		SafeRelease(UAV);
	}
	for (ID3D11ShaderResourceView*& SRV : HZBMipSRVs)
	{
		SafeRelease(SRV);
	}
	HZBMipUAVs.Empty();
	HZBMipSRVs.Empty();
	SafeRelease(HZBAllMipsSRV);
	SafeRelease(HZBTexture);
	HZBWidth = 0;
	HZBHeight = 0;
	HZBMipCount = 0;
}

void FGPUOcclusionCuller::ReleaseReadbackResources()
{
	for (FReadbackSlot& Slot : ReadbackSlots)
	{
		SafeRelease(Slot.StagingBuffer);
		Slot.ComponentIndices.Empty();
		Slot.bPending = false;
	}
}

void FGPUOcclusionCuller::Release()
{
	if (RHI)
	{
		RHI->UnbindComputeResources();
	}
	ReleaseReadbackResources();
	ReleaseCandidateResources();
	ReleaseHZBResources();
	SafeRelease(HZBInitConstantBuffer);
	SafeRelease(HZBReduceConstantBuffer);
	SafeRelease(CullConstantBuffer);
	HZBInitShader = nullptr;
	HZBReduceShader = nullptr;
	OcclusionCullShader = nullptr;
	CachedBoundsByComponentIndex.clear();
	CachedBoundsValid.clear();
	BoundsCacheSpatialRevision = 0;
	RHI = nullptr;
	bEnabled = false;
	ResetVisibilityState();
}
