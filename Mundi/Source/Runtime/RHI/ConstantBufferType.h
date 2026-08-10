#pragma once
// b0 in VS    
#include "Color.h"
#include "LightManager.h"

struct ModelBufferType // b0
{
    FMatrix Model;
    FMatrix ModelInverseTranspose;  // For correct normal transformation with non-uniform scale
};

struct ViewProjBufferType // b1 ê³ ìœ ë²ˆí˜¸ ê³ ì •
{
    FMatrix View;
    FMatrix Proj;
    FMatrix InvView;
    FMatrix InvProj;
};

struct DecalBufferType
{
    FMatrix DecalMatrix;
    float Opacity;
};

// Fireball material parameters (b6 in PS)
struct FireballBufferType
{
    float Time;
    FVector2D Resolution;
    float Padding0; // align to 16 bytes

    FVector CameraPosition;
    float Padding1; // align to 16 bytes

    FVector2D UVScrollSpeed;
    FVector2D UVRotateRad;

    uint32 LayerCount;
    float LayerDivBase;
    float GateK;
    float IntensityScale;
};

struct PostProcessBufferType // b0
{
    float Near;
    float Far;
    int IsOrthographic; // 0 = Perspective, 1 = Orthographic
    float Padding; // 16ë°”ì´íŠ¸ ì •ë ¬ì„ ìœ„í•œ íŒ¨ë”©
};

struct FogBufferType // b2
{
    float FogDensity;
    float FogHeightFalloff;
    float StartDistance;
    float FogCutoffDistance;

    FVector4 FogInscatteringColor; // 16 bytes alignment ìœ„í•´ ì¤‘ê°„ì— ë„£ìŒ

    float FogMaxOpacity;
    float FogHeight; // fog base height
    float Padding[2]; // 16ë°”ì´íŠ¸ ì •ë ¬ì„ ìœ„í•œ íŒ¨ë”©
};

struct alignas(16) FFadeInOutBufferType // b2
{
    FLinearColor FadeColor = FLinearColor(0, 0, 0, 1);  //ë³´í†µ (0, 0, 0, 1)
    
    float Opacity = 0.0f;   // 0~1
    float Weight  = 1.0f;
    float _Pad[2] = {0,0};
};
static_assert(sizeof(FFadeInOutBufferType) % 16 == 0, "CB must be 16-byte aligned");

struct alignas(16) FVinetteBufferType // b2
{
    FLinearColor Color;
    
    float     Radius    = 0.35f;                        // íš¨ê³¼ ì‹œìž‘ ë°˜ê²½(0~1)
    float     Softness  = 0.25f;                        // íŽ˜ë” í­(0~1)
    float     Intensity = 1.0f;                       // ì»¬ëŸ¬ ë¸”ë Œë“œ ê°•ë„ (0~1)
    float     Roundness = 2.0f;                       // 1=ë§ˆë¦„ëª¨, 2= ì›í˜•, >1 ë” ë„¤ëª¨ì— ê°€ê¹ê²Œ
    
    float     Weight    = 1.0f;
    float     _Pad0[3];
};
static_assert(sizeof(FVinetteBufferType) % 16 == 0, "CB must be 16-byte aligned");

struct alignas(16) FBloomSettingsBufferType
{
    FVector4 Params0; // x=Threshold, y=SoftKnee, z=Intensity, w=Mode
    FVector4 Params1; // x=InvWidth, y=InvHeight, z=BlurRadius, w=unused
    FVector4 Params2; // x=DirectionX, y=DirectionY, z=unused, w=unused
};
static_assert(sizeof(FBloomSettingsBufferType) % 16 == 0, "CB must be 16-byte aligned");

struct alignas(16) FGammaCorrectionBufferType
{
    float Gamma;
    float Padding[3];
};
static_assert(sizeof(FGammaCorrectionBufferType) % 16 == 0, "CB must be 16-byte aligned");

// DOF Setup Pass (b2)
struct alignas(16) FDOFSetupBufferType
{
    float FocalDistance;           // m (ì´ˆì  ê±°ë¦¬)
    float FocalRegion;             // m (ì™„ì „ ì„ ëª… ì˜ì—­)
    float NearTransitionRegion;    // m (ê·¼ê²½ ë¸”ëŸ¬ ì „í™˜ ì˜ì—­)
    float FarTransitionRegion;     // m (ì›ê²½ ë¸”ëŸ¬ ì „í™˜ ì˜ì—­)

    float MaxNearBlurSize;         // pixels (ê·¼ê²½ ìµœëŒ€ ë¸”ëŸ¬)
    float MaxFarBlurSize;          // pixels (ì›ê²½ ìµœëŒ€ ë¸”ëŸ¬)
    float NearClip;
    float FarClip;

    int32 IsOrthographic;          // 0 = Perspective, 1 = Orthographic
    FVector _Pad0;
};
static_assert(sizeof(FDOFSetupBufferType) % 16 == 0, "CB must be 16-byte aligned");

// DOF Blur Pass (b2)
struct alignas(16) FDOFBlurBufferType
{
    FVector2D BlurDirection;       // (1,0) = Horizontal, (0,1) = Vertical
    float BlurRadius;              // ë¸”ëŸ¬ ë°˜ê²½ ìŠ¤ì¼€ì¼
    int32 IsFarField;              // 1 = Far Field, 0 = Near Field
};
static_assert(sizeof(FDOFBlurBufferType) % 16 == 0, "CB must be 16-byte aligned");

// DOF Recombine Pass (b2)
struct alignas(16) FDOFRecombineBufferType
{
    float FocalDistance;           // m (ì´ˆì  ê±°ë¦¬)
    float FocalRegion;             // m (ì™„ì „ ì„ ëª… ì˜ì—­)
    float NearTransitionRegion;    // m (ê·¼ê²½ ë¸”ëŸ¬ ì „í™˜ ì˜ì—­)
    float FarTransitionRegion;     // m (ì›ê²½ ë¸”ëŸ¬ ì „í™˜ ì˜ì—­)

    float MaxNearBlurSize;         // pixels (ê·¼ê²½ ìµœëŒ€ ë¸”ëŸ¬)
    float MaxFarBlurSize;          // pixels (ì›ê²½ ìµœëŒ€ ë¸”ëŸ¬)
    float NearClip;
    float FarClip;

    int32 IsOrthographic;
    float _Pad0;
    FVector2D ViewRectMinUV;       // ViewRect ì‹œìž‘ UV (ê²Œìž„ ì˜ì—­)

    FVector2D ViewRectMaxUV;       // ViewRect ë UV (ê²Œìž„ ì˜ì—­)
    FVector2D _Pad1;
};
static_assert(sizeof(FDOFRecombineBufferType) % 16 == 0, "CB must be 16-byte aligned");

struct FXAABufferType // b2
{
    FVector2D InvScreenSize; // 1.0f / ScreenSize (í”½ì…€ í•˜ë‚˜ì˜ í¬ê¸°)
    float SpanMax;           // ìµœëŒ€ íƒìƒ‰ ë²”ìœ„ (8.0f ê¶Œìž¥)
    float ReduceMul;         // ê°ì‡  ìŠ¹ìˆ˜ (1/8 = 0.125f ê¶Œìž¥)

    float ReduceMin;         // ìµœì†Œ ê°ì‡  ê°’ (1/128 = 0.0078125f ê¶Œìž¥)
    float SubPixBlend;       // ì„œë¸Œí”½ì…€ ë¸”ë Œë”© ê°•ë„ (0.75~1.0 ê¶Œìž¥)
    float Padding[2];        // 16ë°”ì´íŠ¸ ì •ë ¬
};

// b0 in PS
struct FMaterialInPs
{
    FVector DiffuseColor; // Kd
    float OpticalDensity; // Ni

    FVector AmbientColor; // Ka
    float Transparency; // Tr Or d

    FVector SpecularColor; // Ks
    float SpecularExponent; // Ns

    FVector EmissiveColor; // Ke
    uint32 IlluminationModel; // illum. Default illumination model to Phong for non-Pbr materials

    FVector TransmissionFilter; // Tf
    float dummy; // 4 bytes padding
    FMaterialInPs() = default;
    FMaterialInPs(const FMaterialInfo& MaterialInfo)
        :DiffuseColor(MaterialInfo.DiffuseColor),
        OpticalDensity(MaterialInfo.OpticalDensity),
        AmbientColor(MaterialInfo.AmbientColor),
        Transparency(MaterialInfo.Transparency),
        SpecularColor(MaterialInfo.SpecularColor),
        SpecularExponent(MaterialInfo.SpecularExponent),
        EmissiveColor(MaterialInfo.EmissiveColor),
        IlluminationModel(MaterialInfo.IlluminationModel),
        TransmissionFilter(MaterialInfo.TransmissionFilter),
        dummy(0)
    { 

    }
};


struct FPixelConstBufferType
{
    FMaterialInPs Material;
    uint32 bHasMaterial;
    uint32 bHasDiffuseTexture;
    uint32 bHasNormalTexture;
	uint32 bHasORMTexture;	// ORM í…ìŠ¤ì²˜ ìœ ë¬´ (Occlusion, Roughness, Metallic)
};

struct ColorBufferType // b3
{
    FLinearColor Color;
    uint32 UUID;
    FVector Padding;
};

struct FLightBufferType
{
    FAmbientLightInfo AmbientLight;
    FDirectionalLightInfo DirectionalLight;

    uint32 PointLightCount;
    uint32 SpotLightCount;
    FVector2D Padding;
};

// b10 ê³ ìœ ë²ˆí˜¸ ê³ ì •
struct FViewportConstants
{
    // x = Viewport TopLeftX
    // y = Viewport TopLeftY
    // z = Viewport Width
    // w = Viewport Height
    FVector4 ViewportRect;

    // x = Screen Width (ì „ì²´ ë Œë” íƒ€ê²Ÿ ë„ˆë¹„)
    // y = Screen Height (ì „ì²´ ë Œë” íƒ€ê²Ÿ ë†’ì´)
    // z = 1.0f / Screen Width
    // w = 1.0f / Screen Height
    FVector4 ScreenSize;
};

struct CameraBufferType // b7
{
    FVector CameraPosition;
    float Padding;
};

// b11: íƒ€ì¼ ê¸°ë°˜ ë¼ì´íŠ¸ ì»¬ë§ ìƒìˆ˜ ë²„í¼
struct FTileCullingBufferType
{
    uint32 TileSize;          // íƒ€ì¼ í¬ê¸° (í”½ì…€, ê¸°ë³¸ 16)
    uint32 TileCountX;        // ê°€ë¡œ íƒ€ì¼ ê°œìˆ˜
    uint32 TileCountY;        // ì„¸ë¡œ íƒ€ì¼ ê°œìˆ˜
    uint32 bUseTileCulling;   // íƒ€ì¼ ì»¬ë§ í™œì„±í™” ì—¬ë¶€ (0=ë¹„í™œì„±í™”, 1=í™œì„±í™”)
    uint32 ViewportStartX;    // ë·°í¬íŠ¸ ì‹œìž‘ X ì¢Œí‘œ
    uint32 ViewportStartY;    // ë·°í¬íŠ¸ ì‹œìž‘ Y ì¢Œí‘œ
    uint32 Padding[2];
};

struct FPointLightShadowBufferType
{
    FMatrix LightViewProjection[6]; // ê° íë¸Œë§µ ë©´ì— ëŒ€í•œ ë·°-í”„ë¡œì ì…˜ í–‰ë ¬ (6ê°œ)
    FVector LightPosition;          // ë¼ì´íŠ¸ì˜ ì›”ë“œ ê³µê°„ ìœ„ì¹˜
    float FarPlane;                 // ì„€ë„ìš° ë§µì˜ ì›ê±°ë¦¬ í‰ë©´ (ê¹Šì´ ë²”ìœ„ ê³„ì‚°ìš©)
    uint32 LightIndex;              // í˜„ìž¬ ë Œë”ë§ ì¤‘ì¸ ë¼ì´íŠ¸ ì¸ë±ìŠ¤
    FVector Padding;                // 16ë°”ì´íŠ¸ ì •ë ¬
};

// b2: SubUV íŒŒë¼ë¯¸í„° (íŒŒí‹°í´ ìŠ¤í”„ë¼ì´íŠ¸ ì‹œíŠ¸ ì• ë‹ˆë©”ì´ì…˜ìš©)
struct FSubUVBufferType
{
    int32 SubImages_Horizontal;  // NX (ê°€ë¡œ íƒ€ì¼ ìˆ˜)
    int32 SubImages_Vertical;    // NY (ì„¸ë¡œ íƒ€ì¼ ìˆ˜)
    int32 InterpMethod;          // 0=None, 1=LinearBlend
    float Padding0;              // 16ë°”ì´íŠ¸ ì •ë ¬
};

// b3: íŒŒí‹°í´ ì´ë¯¸í„° íŒŒë¼ë¯¸í„°
struct FParticleEmitterType
{
    uint32 ScreenAlignment;  // Screen Alignment (0 - Camera, 1 - Velocity)
    uint32 BlendMode;        // EMaterialBlendMode
    FVector2D Padding0;      // 16ë°”ì´íŠ¸ ì •ë ¬
};

// b9: Sky Sphere Parameters
struct alignas(16) FSkyConstantBuffer
{
    FLinearColor ZenithColor;      // Sky top color (16 bytes)
    FLinearColor HorizonColor;     // Horizon color (16 bytes)
    FLinearColor GroundColor;      // Ground color (16 bytes)
    FVector4 SunDirection;         // Sun direction XYZ + padding (16 bytes)
    FLinearColor SunColor;         // Sun color RGB + Intensity in A (16 bytes)
    float HorizonFalloff;          // Gradient falloff (1.0-10.0)
    float SunDiskSize;             // Sun disk size (0.0-1.0)
    float OverallBrightness;       // Overall brightness scale
    float _Padding;                // 16-byte alignment padding
};  // Total: 96 bytes
static_assert(sizeof(FSkyConstantBuffer) % 16 == 0, "FSkyConstantBuffer must be 16-byte aligned");

// b13: Wind Parameters Buffer (VS) - Global wind settings for foliage
struct alignas(16) FWindBufferType
{
    FVector WindDirection;      // Normalized wind direction (X, Y in Z-up system)
    float WindSpeed;            // Animation speed multiplier

    float WindStrength;         // Overall displacement magnitude (units)
    float GustStrength;         // Gust intensity multiplier (0-1)
    float GustFrequency;        // Gust wave frequency
    float Time;                 // Global time in seconds

    float PrimaryFrequency;     // Main sway frequency (~1.2 Hz)
    float SecondaryFrequency;   // Branch movement frequency (~2.5 Hz)
    float TertiaryFrequency;    // Leaf flutter frequency (~6.0 Hz)
    float HeightFalloffPower;   // Height mask curve power (2.0 = quadratic)

    uint32 bEnableWind;         // Per-mesh enable flag (0 or 1)
    float MeshMaxHeight;        // Per-mesh height reference
    FVector2D _Padding;         // 16-byte alignment
};
static_assert(sizeof(FWindBufferType) % 16 == 0, "FWindBufferType must be 16-byte aligned");

#define CONSTANT_BUFFER_INFO(TYPE, SLOT, VS, PS) \
constexpr uint32 TYPE##Slot = SLOT;\
constexpr bool TYPE##IsVS = VS;\
constexpr bool TYPE##IsPS = PS;

//ë§¤í¬ë¡œë¥¼ ì¸ìžë¡œ ë°›ê³  ê·¸ ë§¤í¬ë¡œ í•¨ìˆ˜ì— ë²„í¼ ì „ë‹¬
#define CONSTANT_BUFFER_LIST(MACRO) \
MACRO(ModelBufferType)              \
MACRO(DecalBufferType)              \
MACRO(FireballBufferType)           \
MACRO(PostProcessBufferType)        \
MACRO(FogBufferType)                \
MACRO(FFadeInOutBufferType)         \
MACRO(FGammaCorrectionBufferType)   \
MACRO(FVinetteBufferType)           \
MACRO(FBloomSettingsBufferType)     \
MACRO(FXAABufferType)               \
MACRO(FDOFSetupBufferType)          \
MACRO(FDOFBlurBufferType)           \
MACRO(FDOFRecombineBufferType)      \
MACRO(FPixelConstBufferType)        \
MACRO(ViewProjBufferType)           \
MACRO(ColorBufferType)              \
MACRO(CameraBufferType)             \
MACRO(FLightBufferType)             \
MACRO(FViewportConstants)           \
MACRO(FTileCullingBufferType)       \
MACRO(FPointLightShadowBufferType)  \
MACRO(FSubUVBufferType) \
MACRO(FParticleEmitterType) \
MACRO(FSkyConstantBuffer) \
MACRO(FWindBufferType)

// 16 ë°”ì´íŠ¸ íŒ¨ë”© ì–´ì°íŠ¸
#define STATIC_ASSERT_CBUFFER_ALIGNMENT(Type) \
    static_assert(sizeof(Type) % 16 == 0, "[ " #Type " ] Bad Size. Needs 16-Byte Padding.");
CONSTANT_BUFFER_LIST(STATIC_ASSERT_CBUFFER_ALIGNMENT)

//VS, PS ì„¸íŒ…ì€ í•¨ìˆ˜ íŒŒë¼ë¯¸í„°ë¡œ ê²°ì •í•˜ê²Œ í•˜ëŠ”ê²Œ í›¨ì”¬ ë‚˜ì„ë“¯ ë‚˜ì¤‘ì— ìˆ˜ì • í•„ìš”
//ê·¸ë¦¬ê³  UV Scroll ìƒìˆ˜ë²„í¼ë„ ì²˜ë¦¬í•´ì¤˜ì•¼í•¨
CONSTANT_BUFFER_INFO(ModelBufferType, 0, true, false)
CONSTANT_BUFFER_INFO(PostProcessBufferType, 0, false, true)
CONSTANT_BUFFER_INFO(ViewProjBufferType, 1, true, true) // b1 ì¹´ë©”ë¼ í–‰ë ¬ ê³ ì •
CONSTANT_BUFFER_INFO(FogBufferType, 2, false, true)
CONSTANT_BUFFER_INFO(FFadeInOutBufferType, 2, false, true)
CONSTANT_BUFFER_INFO(FGammaCorrectionBufferType, 2, false, true)
CONSTANT_BUFFER_INFO(FVinetteBufferType, 2, false, true)
CONSTANT_BUFFER_INFO(FBloomSettingsBufferType, 2, false, true)
CONSTANT_BUFFER_INFO(FXAABufferType, 2, false, true)
CONSTANT_BUFFER_INFO(FDOFSetupBufferType, 2, false, true)      // b2, PS only (DOF Setup Pass)
CONSTANT_BUFFER_INFO(FDOFBlurBufferType, 2, false, true)       // b2, PS only (DOF Blur Pass)
CONSTANT_BUFFER_INFO(FDOFRecombineBufferType, 2, false, true)  // b2, PS only (DOF Recombine Pass)
CONSTANT_BUFFER_INFO(ColorBufferType, 3, true, true)   // b3 color
CONSTANT_BUFFER_INFO(FPixelConstBufferType, 4, true, true) // GOURAUDì—ë„ ì‚¬ìš©ë˜ë¯€ë¡œ VSë„ true
CONSTANT_BUFFER_INFO(DecalBufferType, 6, true, true)
CONSTANT_BUFFER_INFO(FireballBufferType, 6, false, true)
CONSTANT_BUFFER_INFO(CameraBufferType, 7, true, true)  // b7, VS+PS (UberLit.hlslê³¼ ì¼ì¹˜)
CONSTANT_BUFFER_INFO(FLightBufferType, 8, true, true)
CONSTANT_BUFFER_INFO(FViewportConstants, 10, true, true)   // ë·° í¬íŠ¸ í¬ê¸°ì— ë”°ë¼ ì „ì²´ í™”ë©´ ë³µì‚¬ë¥¼ ë³´ì •í•˜ê¸° ìœ„í•´ ì„¤ì • (10ë²ˆ ê³ ìœ ë²ˆí˜¸ë¡œ ì‚¬ìš©)
CONSTANT_BUFFER_INFO(FTileCullingBufferType, 11, false, true)  // b11, PS only (UberLit.hlslê³¼ ì¼ì¹˜)
CONSTANT_BUFFER_INFO(FPointLightShadowBufferType, 12, true, true)  // b12, VS+PS
CONSTANT_BUFFER_INFO(FSubUVBufferType, 2, true, true)  // b2, VS+PS (ParticleSprite.hlslìš©)
CONSTANT_BUFFER_INFO(FParticleEmitterType, 3, true, true)  // b3, VS+PS (ParticleSprite.hlsl
CONSTANT_BUFFER_INFO(FSkyConstantBuffer, 9, true, true)    // b9, VS+PS (Sky.hlslìš©)
CONSTANT_BUFFER_INFO(FWindBufferType, 13, true, false)     // b13, VS only (Wind animation)

