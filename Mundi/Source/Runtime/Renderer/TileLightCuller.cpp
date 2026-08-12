#include "pch.h"
#include "TileLightCuller.h"

#include "ResourceManager.h"
#include "Shader.h"

#include <algorithm>

FTileLightCuller::FTileLightCuller() = default;

FTileLightCuller::~FTileLightCuller()
{
	Release();
}

void FTileLightCuller::Initialize(D3D11RHI* InRHI, UINT InTileSize)
{
	if (RHI && RHI != InRHI)
	{
		Release();
	}

	RHI = InRHI;
	TileSize = std::max<UINT>(1, InTileSize);
	if (!RHI)
	{
		return;
	}

	if (!ForwardPlusConstantBuffer)
	{
		D3D11_BUFFER_DESC BufferDesc{};
		BufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		BufferDesc.ByteWidth = sizeof(FForwardPlusConstants);
		BufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		BufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (FAILED(RHI->GetDevice()->CreateBuffer(&BufferDesc, nullptr, &ForwardPlusConstantBuffer)))
		{
			UE_LOG("Forward+: 상수 버퍼 생성 실패\n");
		}
	}

	if (!TileDepthRangeShader)
	{
		TileDepthRangeShader = UResourceManager::GetInstance().Load<UShader>("Shaders/Lighting/TileDepthRange_CS.hlsl");
	}
	if (!TileLightCullingShader)
	{
		TileLightCullingShader = UResourceManager::GetInstance().Load<UShader>("Shaders/Lighting/TileLightCulling_CS.hlsl");
	}
}

bool FTileLightCuller::CullLights(
	ID3D11ShaderResourceView* SceneDepthSRV,
	ID3D11ShaderResourceView* PointLightSRV,
	ID3D11ShaderResourceView* SpotLightSRV,
	UINT PointLightCount,
	UINT SpotLightCount,
	const FMatrix& ViewMatrix,
	const FMatrix& ProjMatrix,
	bool bOrthographic,
	float NearPlane,
	float FarPlane,
	UINT ViewportStartX,
	UINT ViewportStartY,
	UINT ViewportWidth,
	UINT ViewportHeight)
{
	if (!RHI || !SceneDepthSRV || !PointLightSRV || !SpotLightSRV || !ForwardPlusConstantBuffer ||
		!TileDepthRangeShader || !TileLightCullingShader || ViewportWidth == 0 || ViewportHeight == 0)
	{
		return false;
	}

	ID3D11ComputeShader* DepthRangeCS = TileDepthRangeShader->GetComputeShader();
	ID3D11ComputeShader* LightCullingCS = TileLightCullingShader->GetComputeShader();
	if (!DepthRangeCS || !LightCullingCS)
	{
		return false;
	}

	TileCountX = (ViewportWidth + TileSize - 1) / TileSize;
	TileCountY = (ViewportHeight + TileSize - 1) / TileSize;
	TotalTileCount = TileCountX * TileCountY;
	if (!EnsureTileCapacity(TotalTileCount))
	{
		return false;
	}

	FForwardPlusConstants Constants{};
	const FMatrix InverseProjection = bOrthographic
		? ProjMatrix.InverseOrthographicProjection()
		: ProjMatrix.InversePerspectiveProjection();
	Constants.InverseViewProjection = InverseProjection * ViewMatrix.InverseAffine();
	Constants.ViewportStartX = ViewportStartX;
	Constants.ViewportStartY = ViewportStartY;
	Constants.ViewportWidth = ViewportWidth;
	Constants.ViewportHeight = ViewportHeight;
	Constants.TileSize = TileSize;
	Constants.TileCountX = TileCountX;
	Constants.TileCountY = TileCountY;
	Constants.MaxLightsPerTile = MaxLightsPerTile;
	Constants.PointLightCount = PointLightCount;
	Constants.SpotLightCount = SpotLightCount;
	Constants.bOrthographic = bOrthographic ? 1u : 0u;
	Constants.NearPlane = NearPlane;
	Constants.FarPlane = FarPlane;
	if (!UpdateConstants(Constants))
	{
		return false;
	}

	ID3D11DeviceContext* Context = RHI->GetDeviceContext();

	// 지난 뷰에서 PS에 남아 있을 수 있는 같은 리소스를 UAV로 묶기 전에 해제합니다.
	ID3D11ShaderResourceView* NullPSResource = nullptr;
	Context->PSSetShaderResources(2, 1, &NullPSResource);
	Context->PSSetShaderResources(5, 1, &NullPSResource);
	RHI->OMSetCustomRenderTargets(0, nullptr, nullptr);

	// 1) Camera depth -> tile별 device-Z min/max
	RHI->CSSetShader(DepthRangeCS);
	RHI->CSSetConstantBuffers(0, 1, &ForwardPlusConstantBuffer);
	RHI->CSSetShaderResources(0, 1, &SceneDepthSRV);
	RHI->CSSetUnorderedAccessViews(0, 1, &DepthRangeBufferUAV);
	RHI->Dispatch((TileCountX + 7) / 8, (TileCountY + 7) / 8, 1);
	RHI->UnbindComputeResources();

	// 2) 깊이로 잘린 3D tile frustum -> tile별 point/spot light 목록
	ID3D11ShaderResourceView* LightCullSRVs[3] = { DepthRangeBufferSRV, PointLightSRV, SpotLightSRV };
	RHI->CSSetShader(LightCullingCS);
	RHI->CSSetConstantBuffers(0, 1, &ForwardPlusConstantBuffer);
	RHI->CSSetShaderResources(0, 3, LightCullSRVs);
	RHI->CSSetUnorderedAccessViews(0, 1, &LightIndexBufferUAV);
	RHI->Dispatch((TileCountX + 7) / 8, (TileCountY + 7) / 8, 1);
	RHI->UnbindComputeResources();

	Stats.Reset();
	Stats.TileCountX = TileCountX;
	Stats.TileCountY = TileCountY;
	Stats.TotalTileCount = TotalTileCount;
	Stats.TotalPointLights = PointLightCount;
	Stats.TotalSpotLights = SpotLightCount;
	Stats.TotalLights = PointLightCount + SpotLightCount;
	Stats.TotalLightTests = TotalTileCount * Stats.TotalLights;
	Stats.LightIndexBufferSizeBytes = TotalTileCount * MaxLightsPerTile * sizeof(uint32);
	Stats.bGPUGenerated = true;

	return true;
}

bool FTileLightCuller::EnsureTileCapacity(UINT RequiredTileCount)
{
	if (RequiredTileCount == 0)
	{
		return false;
	}
	if (RequiredTileCount <= TileCapacity && DepthRangeBuffer && LightIndexBuffer)
	{
		return true;
	}

	const UINT DoubledCapacity = TileCapacity > 0 ? TileCapacity * 2 : 256;
	const UINT NewTileCapacity = std::max(RequiredTileCount, DoubledCapacity);

	ID3D11Buffer* NewDepthBuffer = nullptr;
	ID3D11ShaderResourceView* NewDepthSRV = nullptr;
	ID3D11UnorderedAccessView* NewDepthUAV = nullptr;
	if (FAILED(RHI->CreateGPUWritableStructuredBuffer(
		sizeof(float) * 2, NewTileCapacity, &NewDepthBuffer, &NewDepthSRV, &NewDepthUAV)))
	{
		UE_LOG("Forward+: tile depth range 버퍼 생성 실패\n");
		return false;
	}

	ID3D11Buffer* NewLightBuffer = nullptr;
	ID3D11ShaderResourceView* NewLightSRV = nullptr;
	ID3D11UnorderedAccessView* NewLightUAV = nullptr;
	if (FAILED(RHI->CreateGPUWritableStructuredBuffer(
		sizeof(uint32), NewTileCapacity * MaxLightsPerTile, &NewLightBuffer, &NewLightSRV, &NewLightUAV)))
	{
		NewDepthUAV->Release();
		NewDepthSRV->Release();
		NewDepthBuffer->Release();
		UE_LOG("Forward+: tile light index 버퍼 생성 실패\n");
		return false;
	}

	ReleaseTileBuffers();
	DepthRangeBuffer = NewDepthBuffer;
	DepthRangeBufferSRV = NewDepthSRV;
	DepthRangeBufferUAV = NewDepthUAV;
	LightIndexBuffer = NewLightBuffer;
	LightIndexBufferSRV = NewLightSRV;
	LightIndexBufferUAV = NewLightUAV;
	TileCapacity = NewTileCapacity;
	return true;
}

bool FTileLightCuller::UpdateConstants(const FForwardPlusConstants& Constants)
{
	D3D11_MAPPED_SUBRESOURCE Mapped{};
	HRESULT Hr = RHI->GetDeviceContext()->Map(
		ForwardPlusConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped);
	if (FAILED(Hr))
	{
		return false;
	}

	memcpy(Mapped.pData, &Constants, sizeof(Constants));
	RHI->GetDeviceContext()->Unmap(ForwardPlusConstantBuffer, 0);
	return true;
}

void FTileLightCuller::ReleaseTileBuffers()
{
	if (LightIndexBufferUAV) { LightIndexBufferUAV->Release(); LightIndexBufferUAV = nullptr; }
	if (LightIndexBufferSRV) { LightIndexBufferSRV->Release(); LightIndexBufferSRV = nullptr; }
	if (LightIndexBuffer) { LightIndexBuffer->Release(); LightIndexBuffer = nullptr; }

	if (DepthRangeBufferUAV) { DepthRangeBufferUAV->Release(); DepthRangeBufferUAV = nullptr; }
	if (DepthRangeBufferSRV) { DepthRangeBufferSRV->Release(); DepthRangeBufferSRV = nullptr; }
	if (DepthRangeBuffer) { DepthRangeBuffer->Release(); DepthRangeBuffer = nullptr; }

	TileCapacity = 0;
}

void FTileLightCuller::Release()
{
	if (RHI)
	{
		RHI->UnbindComputeResources();
	}
	ReleaseTileBuffers();
	if (ForwardPlusConstantBuffer)
	{
		ForwardPlusConstantBuffer->Release();
		ForwardPlusConstantBuffer = nullptr;
	}

	TileDepthRangeShader = nullptr;
	TileLightCullingShader = nullptr;
	RHI = nullptr;
	TileCountX = 0;
	TileCountY = 0;
	TotalTileCount = 0;
}
