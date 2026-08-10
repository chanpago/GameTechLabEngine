//================================================================================================
// Filename:      UberLit.hlsl
// Description:   오브젝트 표면 렌더링을 위한 기본 Uber 셰이더.
//                Extends StaticMeshShader with full lighting support (Gouraud, Lambert, Phong)
//================================================================================================

// --- 조명 모델 선택 ---
// #define LIGHTING_MODEL_GOURAUD 1
// #define LIGHTING_MODEL_LAMBERT 1
// #define LIGHTING_MODEL_PHONG 1

#ifndef USE_GPU_SKINNING
#define USE_GPU_SKINNING 0
#endif

// --- Material 구조체 (OBJ 머티리얼 정보) ---
// 주의: SPECULAR_COLOR 매크로에서 사용하므로 include 전에 정의 필요
struct FMaterial
{
    float3 DiffuseColor;        // Kd - Diffuse 색상
    float OpticalDensity;       // Ni - 광학 밀도 (굴절률)
    float3 AmbientColor;        // Ka - Ambient 색상
    float Transparency;         // Tr or d - 투명도 (0=불투명, 1=투명)
    float3 SpecularColor;       // Ks - Specular 색상
    float SpecularExponent;     // Ns - Specular 지수 (광택도)
    float3 EmissiveColor;       // Ke - 자체발광 색상
    uint IlluminationModel;     // illum - 조명 모델
    float3 TransmissionFilter;  // Tf - 투과 필터 색상
    float Padding;              // 정렬을 위한 패딩
};

// --- 상수 버퍼 (Constant Buffers) ---
// 조명과 StaticMeshShader 기능을 모두 지원하도록 확장

// b0: ModelBuffer (VS) - ModelBufferType과 정확히 일치 (128 bytes)
cbuffer ModelBuffer : register(b0)
{
    row_major float4x4 WorldMatrix;              // 64 bytes
    row_major float4x4 WorldInverseTranspose;    // 64 bytes - 올바른 노멀 변환을 위함
};

// b1: ViewProjBuffer (VS) - ViewProjBufferType과 일치
cbuffer ViewProjBuffer : register(b1)
{
    row_major float4x4 ViewMatrix;
    row_major float4x4 ProjectionMatrix;
    row_major float4x4 InverseViewMatrix;
    row_major float4x4 InverseProjectionMatrix;
};

// b3: ColorBuffer (PS) - 색상 블렌딩/lerp용
cbuffer ColorBuffer : register(b3)
{
    float4 LerpColor;   // 블렌드할 색상 (알파가 블렌드 양 제어)
    uint UUID;
};

// b4: PixelConstBuffer (VS+PS) - OBJ 파일의 머티리얼 정보
// FPixelConstBufferType과 정확히 일치해야 함!
// 주의: GOURAUD 조명 모델에서는 Vertex Shader에서 사용됨
cbuffer PixelConstBuffer : register(b4)
{
    FMaterial Material;         // 64 bytes
    uint bHasMaterial;          // 4 bytes (HLSL)
    uint bHasTexture;           // 4 bytes (HLSL)
    uint bHasNormalTexture;
    uint bHasORMTexture;        // ORM 텍스처 유무 (Occlusion, Roughness, Metallic)
};

cbuffer FLightShadowmBufferType : register(b5)
{
    row_major float4x4 LightShadowView;
    row_major float4x4 LightShadowViewUV;
    row_major float4x4 LightShadowProj;
    float ShadowBias;
    float SlopeScaledBias;
    float2 ShadowPadding;
};

// b13: Wind Parameters Buffer - Global wind settings for foliage
cbuffer WindBuffer : register(b13)
{
    float3 WindDirection;
    float WindSpeed;
    float WindStrength;
    float GustStrength;
    float GustFrequency;
    float WindTime;
    float PrimaryFrequency;
    float SecondaryFrequency;
    float TertiaryFrequency;
    float HeightFalloffPower;
    uint bEnableWind;
    float MeshMaxHeight;
    float2 _WindPadding;
};

// --- Material.SpecularColor 지원 매크로 ---
// LightingCommon.hlsl의 CalculateSpecular에서 Material.SpecularColor를 사용하도록 설정
// 금속 재질의 컬러 Specular 지원
#define SPECULAR_COLOR (bHasMaterial ? Material.SpecularColor : float3(1.0f, 1.0f, 1.0f))

// --- 텍스처 및 샘플러 리소스 ---
Texture2D g_DiffuseTexColor : register(t0);
Texture2D g_NormalTexColor : register(t1);
// Note: t2 is reserved for tile light indices (StructuredBuffer) via LightingBuffers.hlsl.
// Bind ORM to a non-conflicting slot.
Texture2D g_ORMTexColor : register(t6);     // ORM: R=AO, G=Roughness, B=Metallic (moved from t2 -> t6)
Texture2D g_DirectionalShadowMap : register(t5);
TextureCubeArray g_ShadowAtlasCube : register(t8);
Texture2D g_ShadowAtlas2D : register(t9);
Texture2D<float2> g_VSMShadowAtlas : register(t10);
TextureCubeArray<float2> g_VSMShadowCube : register(t11);   // TODO: 지금은 전달 안 되고, 안 쓰는 중
// (IBL removed)

#if USE_GPU_SKINNING
StructuredBuffer<float4x4> g_SkinnedMatrices : register(t12);
StructuredBuffer<float4x4> g_SkinnedNormalMatrices : register(t13);
#endif

SamplerState g_Sample : register(s0);
SamplerState g_Sample2 : register(s1);
SamplerComparisonState g_ShadowSample : register(s2);
SamplerState g_VSMSampler : register(s3);

// --- 공통 조명 시스템 include ---
#include "../Common/LightStructures.hlsl"
#include "../Common/LightingBuffers.hlsl"
#include "../Common/LightingCommon.hlsl"

#if USE_GPU_SKINNING
float3 SkinPosition(float3 Position, uint4 BoneIndices, float4 BoneWeights)
{
    float4 SkinnedPos = 0.0f;
    [unroll]
    for (uint i = 0; i < 4; i++)
    {
        if (BoneWeights[i] > 0.0f)
        {
            float4x4 BoneMatrix = g_SkinnedMatrices[BoneIndices[i]];
            SkinnedPos += mul(BoneMatrix, float4(Position, 1.0f)) * BoneWeights[i];
        }
    }
    return SkinnedPos.xyz;
}

float3 SkinVector(float3 Vector, uint4 BoneIndices, float4 BoneWeights, StructuredBuffer<float4x4> MatrixBuffer)
{
    float3 SkinnedVector = 0.0f;
    [unroll]
    for (uint i = 0; i < 4; i++)
    {
        if (BoneWeights[i] > 0.0f)
        {
            float4x4 BoneMatrix4x4 = MatrixBuffer[BoneIndices[i]];
            float3x3 BoneMatrix = (float3x3)BoneMatrix4x4;
            SkinnedVector += mul(BoneMatrix, Vector) * BoneWeights[i];
        }
    }

    return normalize(SkinnedVector);
}
#endif

// --- 셰이더 입출력 구조체 ---
struct VS_INPUT
{
    float3 Position : POSITION;
    float3 Normal : NORMAL0;
    float2 TexCoord : TEXCOORD0;
    float4 Tangent : TANGENT0;
    float4 Color : COLOR;
#if USE_GPU_SKINNING
    uint4 BoneIndices : BLENDINDICES0;
    float4 BoneWeights : BLENDWEIGHT0;
#endif        
};

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float3 WorldPos : POSITION;     // World position for per-pixel lighting
    float3 Normal : NORMAL0;
    row_major float3x3 TBN : TBN;
    float4 Color : COLOR;
    float2 TexCoord : TEXCOORD0;
};

struct PS_OUTPUT
{
    float4 Color : SV_Target0;
    uint UUID : SV_Target1;
};

//================================================================================================
// Wind Animation Function
//================================================================================================
float3 CalculateWindDisplacement(float3 localPos, float3 worldPos)
{
    // Early out if wind is disabled
    if (bEnableWind == 0 || WindStrength <= 0.001f)
        return float3(0.0f, 0.0f, 0.0f);

    // Height mask from local Z (Z-up system)
    // Roots stay fixed, tips sway the most
    float heightMask = saturate(localPos.z / max(MeshMaxHeight, 0.1f));
    heightMask = pow(heightMask, HeightFalloffPower);

    // Spatial variation using world position (prevents all trees from swaying in sync)
    float spatialOffset = frac(dot(worldPos.xy, float2(0.123f, 0.456f))) * 10.0f;
    float t = WindTime * WindSpeed + spatialOffset;

    // Multi-layered sine waves for natural motion
    // Primary wave - large trunk sway
    float primaryWave = sin(t * PrimaryFrequency) * 0.6f;
    // Secondary wave - branch movement (phase offset for complexity)
    float secondaryWave = sin(t * SecondaryFrequency + 1.57f) * 0.3f;
    // Tertiary wave - leaf flutter (high frequency, small amplitude)
    float tertiaryWave = sin(t * TertiaryFrequency + spatialOffset) * 0.1f;

    float wave = primaryWave + secondaryWave + tertiaryWave;

    // Gust effect - periodic stronger gusts
    float gustPhase = sin(t * GustFrequency) * 0.5f + 0.5f;
    float gustMultiplier = 1.0f + gustPhase * GustStrength;

    // Final displacement along wind direction
    return WindDirection * wave * gustMultiplier * heightMask * WindStrength;
}

//================================================================================================
// 버텍스 셰이더 (Vertex Shader)
//================================================================================================
PS_INPUT mainVS(VS_INPUT Input)
{
    PS_INPUT Out;

#if USE_GPU_SKINNING
    float3 ModelPosition = SkinPosition(Input.Position, Input.BoneIndices, Input.BoneWeights);
    float3 ModelNormal = SkinVector(Input.Normal, Input.BoneIndices, Input.BoneWeights, g_SkinnedNormalMatrices);
    float3 ModelTangent = SkinVector(Input.Tangent.xyz, Input.BoneIndices, Input.BoneWeights, g_SkinnedMatrices);
#else
    float3 ModelPosition = Input.Position.xyz;
    float3 ModelNormal = Input.Normal.xyz;
    float3 ModelTangent = Input.Tangent.xyz;
#endif

    float4 WorldPos = mul(float4(ModelPosition, 1.0f), WorldMatrix);

    // Apply wind displacement in world space
    WorldPos.xyz += CalculateWindDisplacement(ModelPosition, WorldPos.xyz);

    Out.WorldPos = WorldPos.xyz;

    float4 ViewPos = mul(WorldPos, ViewMatrix);
    Out.Position = mul(ViewPos, ProjectionMatrix);

    float3 WorldNormal = normalize(mul(ModelNormal, (float3x3)WorldInverseTranspose));
    Out.Normal = WorldNormal;

    float3 Tangent = normalize(mul(ModelTangent, (float3x3)WorldMatrix));
    float3 BiTangent = normalize(cross(WorldNormal, Tangent) * Input.Tangent.w);
    row_major float3x3 TBN;
    TBN._m00_m01_m02 = Tangent;
    TBN._m10_m11_m12 = BiTangent;
    TBN._m20_m21_m22 = WorldNormal;

    Out.TBN = TBN;

    Out.TexCoord = Input.TexCoord;

    // 머티리얼의 SpecularExponent 사용, 머티리얼이 없으면 기본값 사용
    float specPower = bHasMaterial ? Material.SpecularExponent : 32.0f;

#if LIGHTING_MODEL_GOURAUD
    // Gouraud Shading: 정점별 조명 계산 (diffuse + specular)
    float3 finalColor = float3(0.0f, 0.0f, 0.0f);

    // Specular를 위한 뷰 방향 계산
    float3 viewDir = normalize(CameraPosition - Out.WorldPos);

    // 베이스 색상 결정 (일관성을 위해 Lambert/Phong과 동일한 로직)
    float4 baseColor = Input.Color;
    if (bHasMaterial)
    {
        // 머티리얼 diffuse 색상 사용
        // 주의: 텍스처는 픽셀 셰이더에서 곱해짐
        baseColor.rgb = Material.DiffuseColor;
        baseColor.a = 1.0f;  // 알파가 올바르게 설정되도록 보장
    }

    // Ambient light (OBJ/MTL 표준: La × Ka)
    // 하이브리드 접근: Ka가 (0,0,0) 또는 (1,1,1)이면 Kd(baseColor) 사용
    float3 Ka = bHasMaterial ? Material.AmbientColor : baseColor.rgb;
    bool bIsDefaultKa = all(abs(Ka) < 0.01f) || all(abs(Ka - 1.0f) < 0.01f);
    if (bIsDefaultKa)
    {
        Ka = baseColor.rgb;
    }
    finalColor += CalculateAmbientLight(AmbientLight, Ka);

    // Directional light (diffuse + specular) - 그림자 제외
    FDirectionalLightInfo dirLightNoShadow = DirectionalLight;
    dirLightNoShadow.bCastShadows = 0;
    finalColor += CalculateDirectionalLight(dirLightNoShadow, Out.WorldPos, viewPos.xyz, worldNormal, viewDir, baseColor, true, specPower, g_ShadowAtlas2D, g_ShadowSample);

    // Point lights (diffuse + specular) - 그림자 제외
    for (int i = 0; i < PointLightCount; i++)
    {
        FPointLightInfo pointLightNoShadow = g_PointLightList[i];
        pointLightNoShadow.bCastShadows = 0;
        finalColor += CalculatePointLight(pointLightNoShadow, Out.WorldPos, worldNormal, viewDir, baseColor, true, specPower, g_ShadowAtlasCube, g_ShadowSample);
    }

    // Spot lights (diffuse + specular) - 그림자 제외
    for (int j = 0; j < SpotLightCount; j++)
    {
        FSpotLightInfo spotLightNoShadow = g_SpotLightList[j];
        spotLightNoShadow.bCastShadows = 0;
        finalColor += CalculateSpotLight(spotLightNoShadow, Out.WorldPos, worldNormal, viewDir, baseColor, true, specPower, g_ShadowAtlas2D, g_ShadowSample, g_VSMShadowAtlas, g_VSMSampler);
    }

    Out.Color = float4(finalColor, baseColor.a);

#elif LIGHTING_MODEL_LAMBERT
    // Lambert Shading: 픽셀별 계산을 위해 픽셀 셰이더로 데이터 전달
    Out.Color = Input.Color;

#elif LIGHTING_MODEL_PHONG
    // Phong Shading: 픽셀별 계산을 위해 픽셀 셰이더로 데이터 전달
    Out.Color = Input.Color;

#else
    // 조명 모델 미정의 - 정점 색상을 그대로 전달
    Out.Color = Input.Color;

#endif

    return Out;
}

//================================================================================================
// 픽셀 셰이더 (Pixel Shader)
//================================================================================================
PS_OUTPUT mainPS(PS_INPUT Input)
{
    PS_OUTPUT Output;
    Output.UUID = UUID;
    
    //CSM 구간 시각화
    float3 Color[2] =
    {
        float3(1, 0, 0),
        float3(0, 1, 0)
    };
    int CascadeCount = DirectionalLight.CascadeCount;

    float4 ViewPos = mul(float4(Input.WorldPos, 1), ViewMatrix);

    float3 CascadeAreaDebugColor;
    float CascadeAreaDebugBlendValue = DirectionalLight.bCascaded ? DirectionalLight.CascadedAreaColorDebugValue : 0;
    for (int i = 0; i < CascadeCount; i++)
    {
        if (ViewPos.z < DirectionalLight.CascadedSliceDepth[(i + 1) / 4][(i + 1) % 4])
        {
            CascadeAreaDebugColor = Color[i % 2];
            break;
        }
    }

    // UV 스크롤링 적용 (활성화된 경우)
    float2 uv = Input.TexCoord;
    //if (bHasMaterial && bHasTexture)
    //{
    //    uv += UVScrollSpeed * UVScrollTime;
    //}

    // 텍스처 샘플링 (머트리얼 색상은 Gouraud는 VS에서 적용됨)
    float4 texColor = g_DiffuseTexColor.Sample(g_Sample, uv);
    // sRGB to Linear 변환 (감마 보정 - 텍스처가 sRGB 공간에 저장되어 있음)
    texColor.rgb = pow(texColor.rgb, 2.2f);

    // Alpha cutout: 텍스처의 알파가 임계값 이하면 픽셀 폐기 (투명 배경 처리)
    if (bHasTexture && texColor.a < 0.5f)
    {
        discard;
    }

#ifdef VIEWMODE_WORLD_NORMAL
    // World Normal 시각화: Normal 벡터를 색상으로 변환
    // Normal 범위: -1~1 → 색상 범위: 0~1
    float3 normalColor = Input.Normal * 0.5 + 0.5;

    if(bHasNormalTexture)
    {
        normalColor = g_NormalTexColor.Sample(g_Sample2, uv);
        normalColor = normalColor * 2.0f - 1.0f;
        normalColor = normalize(mul(normalColor, Input.TBN));
        // 노말 텍스처 경로도 색상 범위 [0,1]로 변환 필요
        normalColor = normalColor * 0.5 + 0.5;
    }

    Output.Color = float4(normalColor, 1.0);
    return Output;
#endif

    // 머트리얼의 SpecularExponent 사용, 머트리얼이 없으면 기본값 사용
    float specPower = bHasMaterial ? Material.SpecularExponent : 32.0f;

#if LIGHTING_MODEL_GOURAUD
    // Gouraud Shading: 조명이 이미 버텍스 셸이더에서 계산됨 (그림자 제외)
    // Pixel Shader에서 그림자 팩터만 계산해서 곱함
    float4 finalPixel = Input.Color;
    
    // 그림자 팩터 계산 (모든 라이트 통합)
    float shadowFactor = 1.0f;
    
    // Directional Light 그림자
    if (DirectionalLight.bCastShadows)
    {
        shadowFactor *= CalculateSpotLightShadowFactor(Input.WorldPos, DirectionalLight.Cascades[0], g_ShadowAtlas2D, g_ShadowSample);
    }
    
    // Point Light 그림자
    for (int i = 0; i < PointLightCount; i++)
    {
        if (g_PointLightList[i].bCastShadows)
        {
            shadowFactor *= CalculatePointLightShadowFactor(
                Input.WorldPos, Input.Normal, g_PointLightList[i], 16, g_ShadowAtlasCube, g_ShadowSample);
        }
    }
    
    // Spot Light 그림자
    for (int j = 0; j < SpotLightCount; j++)
    {
        if (g_SpotLightList[j].bCastShadows)
        {
            shadowFactor *= CalculateSpotLightShadowFactor(
                Input.WorldPos, g_SpotLightList[j].ShadowData, g_ShadowAtlas2D, g_ShadowSample);
        }
    }
    
    // 그림자 적용 (VS에서 계산된 조명)
    finalPixel.rgb *= shadowFactor;

    // 텍스처 또는 머트리얼 색상 적용
    if (bHasTexture)
    {
        // 텍스처 모듈레이션: 조명 결과에 텍스처 곱하기
        finalPixel.rgb *= texColor.rgb;
    }
    // 주의: Material.DiffuseColor는 이미 Vertex Shader에서 적용됨
    // 여기서 추가 색상 적용 불필요

    // 자체발광 추가 (조명의 영향을 받지 않음)
    if (bHasMaterial)
    {
        finalPixel.rgb += Material.EmissiveColor;
    }

    // 비머티리얼 오브젝트의 머티리얼/색상 블렌딩 적용
    if (!bHasMaterial)
    {
        finalPixel.rgb = lerp(finalPixel.rgb, LerpColor.rgb, LerpColor.a);
    }

    // 머티리얼 투명도 적용 (0=불투명, 1=투명)
    if (bHasMaterial)
    {
        finalPixel.a *= (1.0f - Material.Transparency);
    }

    Output.Color = finalPixel;
    Output.Color.rgb = (1 - CascadeAreaDebugBlendValue) * Output.Color.rgb + CascadeAreaDebugBlendValue * CascadeAreaDebugColor;
    return Output;

#elif LIGHTING_MODEL_LAMBERT
    // Lambert Shading: 픽셀별 디퓨즈 조명 계산 (스페큘러 없음)
    float3 normal = normalize(Input.Normal);
    float4 baseColor = Input.Color;

    // 텍스처가 있으면 텍스처로 시작
    if (bHasTexture)
    {
        baseColor.rgb = texColor.rgb;
    }
    else if (bHasMaterial)
    {
        // 텍스처 없음, 머티리얼 diffuse 색상 사용
        baseColor.rgb = Material.DiffuseColor;
    }
    else
    {
        // 텍스처와 머티리얼 모두 없음, LerpColor와 블렌드
        baseColor.rgb = lerp(baseColor.rgb, LerpColor.rgb, LerpColor.a);
    }

    float3 litColor = float3(0.0f, 0.0f, 0.0f);

    // Ambient light (OBJ/MTL 표준: La × Ka)
    // 하이브리드 접근: Ka가 (0,0,0) 또는 (1,1,1)이면 Kd(baseColor) 사용
    float3 Ka = bHasMaterial ? Material.AmbientColor : baseColor.rgb;
    bool bIsDefaultKa = all(abs(Ka) < 0.01f) || all(abs(Ka - 1.0f) < 0.01f);
    if (bIsDefaultKa)
    {
        Ka = baseColor.rgb;
    }
    litColor += CalculateAmbientLight(AmbientLight, Ka);

    // Directional light (diffuse만)
    litColor += CalculateDirectionalLight(DirectionalLight, Input.WorldPos, ViewPos.xyz, normal, float3(0, 0, 0), baseColor, false, 0.0f, g_ShadowAtlas2D, g_ShadowSample);

    // 타일 기반 라이트 컬링 적용 (활성화된 경우)
    if (bUseTileCulling)
    {
        // 현재 픽셀이 속한 타일 계산
        uint tileIndex = CalculateTileIndex(Input.Position, ViewportStartX, ViewportStartY);
        uint tileDataOffset = GetTileDataOffset(tileIndex);

        // 타일에 영향을 주는 라이트 개수
        uint lightCount = g_TileLightIndices[tileDataOffset];

        // 타일 내 라이트만 순회
        [loop]
        for (uint i = 0; i < lightCount; i++)
        {
            uint packedIndex = g_TileLightIndices[tileDataOffset + 1 + i];
            uint lightType = (packedIndex >> 16) & 0xFFFF;  // 상위 16비트: 타입
            uint lightIdx = packedIndex & 0xFFFF;           // 하위 16비트: 인덱스

            if (lightType == 0)  // Point Light
            {
                litColor += CalculatePointLight(g_PointLightList[lightIdx], Input.WorldPos, normal, float3(0, 0, 0), baseColor, false, 0.0f, g_ShadowAtlasCube, g_ShadowSample);
            }
            else if (lightType == 1)  // Spot Light
            {
                litColor +=  CalculateSpotLightPBR(g_SpotLightList[lightIdx], Input.WorldPos, normal,
                    viewDir, float4(diffuseColor, baseColor.a), true,
                    specPower, g_ShadowAtlas2D, g_ShadowSample, g_VSMShadowAtlas, g_VSMSampler, specularColorPBR);
            }
        }
    }
    else
    {
        // 타일 컴링 비활성화: 모든 라이트 순회 (기존 방식)
        for (int i = 0; i < PointLightCount; i++)
        {
            litColor += CalculatePointLight(g_PointLightList[i], Input.WorldPos, normal, float3(0, 0, 0), baseColor, false, 0.0f, g_ShadowAtlasCube, g_ShadowSample);
        }

        [loop]
        for (int j = 0; j < SpotLightCount; j++)
        {
            litColor +=  CalculateSpotLight(g_SpotLightList[j], Input.WorldPos, normal,
                    float3(0, 0, 0), baseColor, false,
                    0.0f, g_ShadowAtlas2D, g_ShadowSample, g_VSMShadowAtlas, g_VSMSampler);
        }
    }

    // 조명 계산 후 자체발광 추가
    if (bHasMaterial)
    {
        litColor += Material.EmissiveColor;
    }

    // 원본 알파 보존 (조명은 투명도에 영향 없음)
    float finalAlpha = baseColor.a;

    // 머티리얼 투명도 적용 (0=불투명, 1=투명)
    if (bHasMaterial)
    {
        finalAlpha *= (1.0f - Material.Transparency);
    }

    Output.Color = float4(litColor, finalAlpha);
    Output.Color.rgb = (1 - CascadeAreaDebugBlendValue) * Output.Color.rgb + CascadeAreaDebugBlendValue * CascadeAreaDebugColor;
    return Output;

#elif LIGHTING_MODEL_PHONG
    // Phong Shading: 픽셀별 디퓨즈와 스페큘러 조명 계산 (Blinn-Phong)
    float3 normal = normalize(Input.Normal);
    if(bHasNormalTexture)
    {
        normal = g_NormalTexColor.Sample(g_Sample2, uv);
        normal = normal * 2.0f - 1.0f;
        normal = normalize(mul(normal, Input.TBN));
    }
    float3 viewDir = normalize(CameraPosition - Input.WorldPos);
    float4 baseColor = Input.Color;

    // ORM 텍스처 샘플링 (R=AO, G=Roughness, B=Metallic)
    float ao = 1.0f;
    float roughness = 0.5f;
    float metallic = 0.0f;
    if (bHasORMTexture)
    {
        float3 orm = g_ORMTexColor.Sample(g_Sample, uv).rgb;
        ao = saturate(orm.r);
        roughness = saturate(orm.g);
        metallic = saturate(orm.b);

        // Stable roughness -> Blinn-Phong exponent mapping to produce visible highlights
        // Roughness 0 -> ~64, Roughness 1 -> ~8 (tunable)
        specPower = lerp(64.0f, 8.0f, roughness);
    }

    // 텍스처가 있으면 텍스처로 시작
    if (bHasTexture)
    {
        baseColor.rgb = texColor.rgb;
    }
    else if (bHasMaterial)
    {
        // 텍스처 없음, 머티리얼 diffuse 색상 사용
        baseColor.rgb = Material.DiffuseColor;
    }
    else
    {
        // 텍스처와 머티리얼 모두 없음, LerpColor와 블렌드
        baseColor.rgb = lerp(baseColor.rgb, LerpColor.rgb, LerpColor.a);
    }

    // Metallic 워크플로우: metallic이 높을수록 diffuse는 줄고 specular가 커짐
    // Add a small diffuse floor to avoid pitch-black metals when albedo is authored black
    float3 diffuseColor = baseColor.rgb * (1.0f - metallic) + 0.02f * metallic;
    float3 F0Dielectric = bHasMaterial ? Material.SpecularColor : float3(0.04f, 0.04f, 0.04f);
    float3 specularColorPBR = lerp(F0Dielectric, baseColor.rgb, metallic);
    // Ensure a minimum F0 so highlights don't disappear for black albedo metals
    specularColorPBR = max(specularColorPBR, float3(0.04f, 0.04f, 0.04f));
    // Heuristic: if asset authoring uses black baseColor for metals, fallback to typical metal F0
    // 0.56 is typical for iron/steel, more realistic than 0.90 (silver/chrome)
    float luma = dot(baseColor.rgb, float3(0.2126f, 0.7152f, 0.0722f));
    float3 defaultMetalF0 = float3(0.56f, 0.56f, 0.56f);
    if (metallic > 0.5f && luma < 0.05f)
    {
        specularColorPBR = lerp(F0Dielectric, defaultMetalF0, metallic);
    }

    float3 litColor = float3(0.0f, 0.0f, 0.0f);

    // Ambient light (OBJ/MTL 표준: La × Ka)
    // 하이브리드 접근: Ka가 (0,0,0) 또는 (1,1,1)이면 Kd(baseColor) 사용
    float3 Ka = bHasMaterial ? Material.AmbientColor : diffuseColor;
    bool bIsDefaultKa = all(abs(Ka) < 0.01f) || all(abs(Ka - 1.0f) < 0.01f);
    if (bIsDefaultKa)
    {
        Ka = diffuseColor;
    }
    // AO 적용 (Ambient Occlusion)
    litColor += CalculateAmbientLight(AmbientLight, Ka) * ao;

    // Directional light (diffuse + specular)
    float3 DirectionalLightColor;
    if (bHasORMTexture)
    {
        DirectionalLightColor = CalculateDirectionalLightPBR(DirectionalLight, Input.WorldPos, ViewPos.xyz, normal, viewDir, float4(diffuseColor, baseColor.a), true, specPower, g_ShadowAtlas2D, g_ShadowSample, specularColorPBR, roughness);
    }
    else
    {
        DirectionalLightColor = CalculateDirectionalLight(DirectionalLight, Input.WorldPos, ViewPos.xyz, normal, viewDir, baseColor, true, specPower, g_ShadowAtlas2D, g_ShadowSample);
    }

    // (IBL removed)

    litColor += DirectionalLightColor;

    // 타일 기반 라이트 컬링 적용 (활성화된 경우)
    if (bUseTileCulling)
    {
        // 현재 픽셀이 속한 타일 계산
        uint tileIndex = CalculateTileIndex(Input.Position, ViewportStartX, ViewportStartY);
        uint tileDataOffset = GetTileDataOffset(tileIndex);

        // 타일에 영향을 주는 라이트 개수
        uint lightCount = g_TileLightIndices[tileDataOffset];

        // 타일 내 라이트만 순회
        [loop]
        for (uint i = 0; i < lightCount; i++)
        {
            uint packedIndex = g_TileLightIndices[tileDataOffset + 1 + i];
            uint lightType = (packedIndex >> 16) & 0xFFFF;  // 상위 16비트: 타입
            uint lightIdx = packedIndex & 0xFFFF;           // 하위 16비트: 인덱스

            if (lightType == 0)  // Point Light
            {
                if (bHasORMTexture)
                {
                    litColor += CalculatePointLightPBR(g_PointLightList[lightIdx], Input.WorldPos, normal, viewDir, float4(diffuseColor, baseColor.a), true, specPower, g_ShadowAtlasCube, g_ShadowSample, specularColorPBR, roughness);
                }
                else
                {
                    litColor += CalculatePointLight(g_PointLightList[lightIdx], Input.WorldPos, normal, viewDir, baseColor, true, specPower, g_ShadowAtlasCube, g_ShadowSample);
                }
            }
            else if (lightType == 1)  // Spot Light
            {
                if (bHasORMTexture)
                {
                    litColor +=  CalculateSpotLightPBR(g_SpotLightList[lightIdx], Input.WorldPos, normal,
                        viewDir, float4(diffuseColor, baseColor.a), true,
                        specPower, g_ShadowAtlas2D, g_ShadowSample, g_VSMShadowAtlas, g_VSMSampler, specularColorPBR, roughness);
                }
                else
                {
                    litColor +=  CalculateSpotLight(g_SpotLightList[lightIdx], Input.WorldPos, normal,
                        viewDir, baseColor, true,
                        specPower, g_ShadowAtlas2D, g_ShadowSample, g_VSMShadowAtlas, g_VSMSampler);
                }
            }
        }
    }
    else
    {
        // 타일 컴링 비활성화: 모든 라이트 순회 (기존 방식)
        for (int i = 0; i < PointLightCount; i++)
        {
            if (bHasORMTexture)
            {
                litColor += CalculatePointLightPBR(g_PointLightList[i], Input.WorldPos, normal, viewDir, float4(diffuseColor, baseColor.a), true, specPower, g_ShadowAtlasCube, g_ShadowSample, specularColorPBR, roughness);
            }
            else
            {
                litColor += CalculatePointLight(g_PointLightList[i], Input.WorldPos, normal, viewDir, baseColor, true, specPower, g_ShadowAtlasCube, g_ShadowSample);
            }
        }
        
        [loop]
        for (int j = 0; j < SpotLightCount; j++)
        {
            if (bHasORMTexture)
            {
                litColor +=  CalculateSpotLightPBR(g_SpotLightList[j],Input.WorldPos, normal, viewDir, float4(diffuseColor, baseColor.a), true, specPower, g_ShadowAtlas2D, g_ShadowSample, g_VSMShadowAtlas, g_VSMSampler, specularColorPBR, roughness);
            }
            else
            {
                litColor +=  CalculateSpotLight(g_SpotLightList[j],Input.WorldPos, normal, viewDir, baseColor, true, specPower, g_ShadowAtlas2D, g_ShadowSample, g_VSMShadowAtlas, g_VSMSampler);
            }
        }
    }

    // 조명 계산 후 자체발광 추가
    if (bHasMaterial)
    {
        litColor += Material.EmissiveColor;
    }

    // 원본 알파 보존 (조명은 투명도에 영향 없음)
    float finalAlpha = baseColor.a;

    // 머티리얼 투명도 적용 (0=불투명, 1=투명)
    if (bHasMaterial)
    {
        finalAlpha *= (1.0f - Material.Transparency);
    }
    
    Output.Color = float4(litColor, finalAlpha);
    Output.Color.rgb = (1 - CascadeAreaDebugBlendValue) * Output.Color.rgb + CascadeAreaDebugBlendValue * CascadeAreaDebugColor;
    return Output;

#else
    // 조명 모델 미정의 - StaticMeshShader 동작 사용
    float4 finalPixel = Input.Color;

    // 머티리얼/텍스처 블렌딩 적용
    if (bHasMaterial)
    {
        finalPixel.rgb = Material.DiffuseColor;
        if (bHasTexture)
        {
            finalPixel.rgb = texColor.rgb;
        }
        // 자체발광 추가
        finalPixel.rgb += Material.EmissiveColor;
    }
    else
    {
        // LerpColor와 블렌드
        finalPixel.rgb = lerp(finalPixel.rgb, LerpColor.rgb, LerpColor.a);
        finalPixel.rgb *= texColor.rgb;
    }

    // 머티리얼 투명도 적용 (0=불투명, 1=투명)
    if (bHasMaterial)
    {
        finalPixel.a *= (1.0f - Material.Transparency);
    }
    
    Output.Color = finalPixel;
    return Output;
#endif
}
