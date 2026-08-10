// Sky Sphere Shader - Procedural gradient sky with sun disk
// b0: ModelBuffer (VS)
cbuffer ModelBuffer : register(b0)
{
    row_major float4x4 WorldMatrix;
    row_major float4x4 WorldInverseTranspose;
};

// b1: ViewProjBuffer (VS+PS)
cbuffer ViewProjBuffer : register(b1)
{
    row_major float4x4 ViewMatrix;
    row_major float4x4 ProjectionMatrix;
    row_major float4x4 InverseViewMatrix;
    row_major float4x4 InverseProjectionMatrix;
};

// b9: Sky Parameters (PS)
cbuffer SkyBuffer : register(b9)
{
    float4 ZenithColor;      // Sky top color (RGB + unused)
    float4 HorizonColor;     // Horizon color (RGB + unused)
    float4 GroundColor;      // Ground/bottom color (RGB + unused)
    float4 SunDirection;     // Normalized sun direction XYZ + padding
    float4 SunColor;         // Sun color RGB + Intensity in A
    float HorizonFalloff;    // Gradient falloff exponent (1.0-10.0)
    float SunDiskSize;       // Sun disk angular size (0.0-1.0)
    float OverallBrightness; // Final brightness multiplier
    float _Padding;          // 16-byte alignment
};

struct VS_INPUT
{
    float3 Position : POSITION;
    float4 Color    : COLOR;
    float2 TexCoord : TEXCOORD0;
    float3 Normal   : NORMAL;
};

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float3 LocalDir : TEXCOORD0;  // Normalized local position (view direction)
};

struct PS_OUTPUT
{
    float4 Color : SV_Target0;
    uint UUID : SV_Target1;
};

// Compute sun disk contribution with soft glow
float ComputeSunDisk(float3 viewDir, float3 sunDir, float diskSize, float intensity)
{
    // Dot product between view direction and sun direction (sun points towards sun)
    float sunDot = dot(viewDir, -sunDir);

    // Map diskSize (0-1) to actual angular threshold
    // Make the disk significantly smaller for the same UI value
    // Previous scale (0.1) produced a larger-than-desired disk
    float diskAngle = diskSize * 0.02f;
    float diskEdge = 1.0f - diskAngle;

    // Sharp disk with soft edge
    float disk = smoothstep(diskEdge - 0.002f, diskEdge + 0.001f, sunDot);

    // Soft glow around sun
    float glowFalloff = 8.0f / max(diskSize, 0.01f);
    float glow = pow(saturate(sunDot), glowFalloff) * 0.3f;

    return (disk + glow) * intensity;
}

// Vertex Shader
PS_INPUT mainVS(VS_INPUT input)
{
    PS_INPUT output;

    // Transform to world space (sphere is centered at camera position)
    float4 worldPos = mul(float4(input.Position, 1.0f), WorldMatrix);

    // Apply view-projection transform
    float4 viewPos = mul(worldPos, ViewMatrix);
    output.Position = mul(viewPos, ProjectionMatrix);

    // Pass normalized local position as direction for gradient calculation
    // This represents the view direction from inside the sphere
    output.LocalDir = normalize(input.Position);

    return output;
}

// Pixel Shader
PS_OUTPUT mainPS(PS_INPUT input)
{
    PS_OUTPUT output;

    float3 dir = normalize(input.LocalDir);

    // Vertical gradient based on Z component (up direction)
    // dir.z ranges from -1 (looking down) to 1 (looking up)
    float heightFactor = dir.z;

    // Compute sky gradient color
    float3 skyColor;

    if (heightFactor > 0.0f)
    {
        // Above horizon: blend from horizon to zenith
        // t=0 at horizon (heightFactor=0), t=1 at zenith (heightFactor=1)
        float t = pow(heightFactor, HorizonFalloff);
        skyColor = lerp(HorizonColor.rgb, ZenithColor.rgb, t);
    }
    else
    {
        // Below horizon: blend from horizon to ground
        // Use softer falloff for ground transition
        float t = pow(-heightFactor, HorizonFalloff * 0.5f);
        skyColor = lerp(HorizonColor.rgb, GroundColor.rgb, t);
    }

    // Add sun disk contribution
    float3 sunDir = normalize(SunDirection.xyz);
    float sunIntensity = SunColor.a;
    float sunContrib = ComputeSunDisk(dir, sunDir, SunDiskSize, sunIntensity);
    skyColor += SunColor.rgb * sunContrib;

    // Apply overall brightness
    skyColor *= OverallBrightness;

    output.Color = float4(skyColor, 1.0f);
    output.UUID = 0;  // Sky is not selectable

    return output;
}
