// Particle Beam Shader
// 빔 이미터 전용 셰이더 (레이저, 번개 등)

// b0: ModelBuffer (VS) - Not used for beams (already in world space)
cbuffer ModelBuffer : register(b0)
{
    row_major float4x4 WorldMatrix;
    row_major float4x4 WorldInverseTranspose;
};

// b1: ViewProjBuffer (VS)
cbuffer ViewProjBuffer : register(b1)
{
    row_major float4x4 ViewMatrix;
    row_major float4x4 ProjectionMatrix;
    row_major float4x4 InverseViewMatrix;
    row_major float4x4 InverseProjectionMatrix;
};

Texture2D BeamTexture : register(t0);
SamplerState BeamSampler : register(s0);

struct VSInput
{
    float3 Position : POSITION;
    float2 UV : TEXCOORD0;
    float4 Color : COLOR;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
    float4 Color : COLOR;
};

PSInput mainVS(VSInput input)
{
    PSInput output;

    // 빔 정점은 이미 월드 좌표이므로 ViewProjection만 적용
    float4 worldPos = float4(input.Position, 1.0);
    float4 viewPos = mul(worldPos, ViewMatrix);
    output.Position = mul(viewPos, ProjectionMatrix);

    output.UV = input.UV;
    output.Color = input.Color;

    return output;
}

float4 mainPS(PSInput input) : SV_TARGET
{
    // 텍스처 샘플링
    float4 TexColor = BeamTexture.Sample(BeamSampler, input.UV);

    float BeamDist = abs(input.UV.x - 0.5f);
    float SoftFalloff = exp(-25.0f * BeamDist * BeamDist); // bell curve falloff across width

    // 파티클 색상과 곱하기
    float CoreWidth = 0.15f;
    float CoreFactor = saturate(1.0f - BeamDist / CoreWidth);
    CoreFactor *= CoreFactor;
    
    float3 CoreColor = float3(1.0f, 1.0f, 1.0f);
    float3 EdgeColor = input.Color.rgb;
        
    float3 BeamColor = lerp(EdgeColor, CoreColor, CoreFactor);
    
    float3 FinalRgb = BeamColor * TexColor.rgb;
    float Alpha = TexColor.a * input.Color.a * SoftFalloff;

    return float4(FinalRgb, Alpha);
}


