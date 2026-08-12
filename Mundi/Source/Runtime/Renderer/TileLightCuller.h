#pragma once

#include "LightManager.h"
#include "TileCullingStats.h"
#include "D3D11RHI.h"

class UShader;

// Camera depth를 이용해 타일별 깊이 범위와 라이트 목록을 GPU에서 생성하는 Forward+ 컬러입니다.
// 리소스는 뷰마다 다시 만들지 않고 필요한 최대 크기까지 확장한 뒤 재사용합니다.
class FTileLightCuller
{
public:
	FTileLightCuller();
	~FTileLightCuller();

	void Initialize(D3D11RHI* InRHI, UINT InTileSize = 16);

	bool CullLights(
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
		UINT ViewportHeight);

	ID3D11ShaderResourceView* GetLightIndexBufferSRV() const { return LightIndexBufferSRV; }
	ID3D11ShaderResourceView* GetDepthRangeBufferSRV() const { return DepthRangeBufferSRV; }
	const FTileCullingStats& GetStats() const { return Stats; }

	UINT GetTileSize() const { return TileSize; }
	UINT GetTileCountX() const { return TileCountX; }
	UINT GetTileCountY() const { return TileCountY; }

	void Release();

private:
	struct alignas(16) FForwardPlusConstants
	{
		FMatrix InverseViewProjection;

		uint32 ViewportStartX = 0;
		uint32 ViewportStartY = 0;
		uint32 ViewportWidth = 0;
		uint32 ViewportHeight = 0;

		uint32 TileSize = 16;
		uint32 TileCountX = 0;
		uint32 TileCountY = 0;
		uint32 MaxLightsPerTile = 256;

		uint32 PointLightCount = 0;
		uint32 SpotLightCount = 0;
		uint32 bOrthographic = 0;
		uint32 Padding0 = 0;

		float NearPlane = 0.1f;
		float FarPlane = 1000.0f;
		float DepthPadding = 0.0005f;
		float Padding1 = 0.0f;
	};

	static_assert(sizeof(FForwardPlusConstants) % 16 == 0, "Forward+ constants must be 16-byte aligned");

	bool EnsureTileCapacity(UINT RequiredTileCount);
	bool UpdateConstants(const FForwardPlusConstants& Constants);
	void ReleaseTileBuffers();

private:
	D3D11RHI* RHI = nullptr;
	UShader* TileDepthRangeShader = nullptr;
	UShader* TileLightCullingShader = nullptr;

	UINT TileSize = 16;
	UINT TileCountX = 0;
	UINT TileCountY = 0;
	UINT TotalTileCount = 0;
	UINT TileCapacity = 0;

	static constexpr UINT MaxLightsPerTile = 256;

	ID3D11Buffer* ForwardPlusConstantBuffer = nullptr;

	ID3D11Buffer* DepthRangeBuffer = nullptr;
	ID3D11ShaderResourceView* DepthRangeBufferSRV = nullptr;
	ID3D11UnorderedAccessView* DepthRangeBufferUAV = nullptr;

	ID3D11Buffer* LightIndexBuffer = nullptr;
	ID3D11ShaderResourceView* LightIndexBufferSRV = nullptr;
	ID3D11UnorderedAccessView* LightIndexBufferUAV = nullptr;

	FTileCullingStats Stats;
};
