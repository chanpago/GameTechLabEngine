#pragma once

#include "SceneComponent.h"
#include "USkySphereComponent.generated.h"

class UShader;
class UBillboardComponent;

/**
 * Sky Sphere Component
 * Renders a procedural gradient sky with sun disk.
 */
UCLASS(DisplayName="Sky Sphere Component", Description="Procedural sky rendering component with gradient and sun disk")
class USkySphereComponent : public USceneComponent
{
public:
    GENERATED_REFLECTION_BODY()

    USkySphereComponent();
    ~USkySphereComponent() override;

    // Component Lifecycle
    void OnRegister(UWorld* InWorld) override;

    // Serialization
    void Serialize(const bool bInIsLoading, JSON& InOutHandle) override;
    void DuplicateSubObjects() override;

    // === Sky Color Parameters ===
    FLinearColor GetZenithColor() const { return ZenithColor; }
    FLinearColor GetHorizonColor() const { return HorizonColor; }
    FLinearColor GetGroundColor() const { return GroundColor; }
    float GetHorizonFalloff() const { return HorizonFalloff; }
    float GetOverallBrightness() const { return OverallBrightness; }

    void SetZenithColor(const FLinearColor& Color) { ZenithColor = Color; }
    void SetHorizonColor(const FLinearColor& Color) { HorizonColor = Color; }
    void SetGroundColor(const FLinearColor& Color) { GroundColor = Color; }
    void SetHorizonFalloff(float Value) { HorizonFalloff = FMath::Clamp(Value, 1.0f, 10.0f); }
    void SetOverallBrightness(float Value) { OverallBrightness = FMath::Max(0.0f, Value); }

    // === Sun Parameters ===
    FVector GetSunDirection() const { return SunDirection.GetSafeNormal(); }
    float GetSunDiskSize() const { return SunDiskSize; }
    FLinearColor GetSunColor() const { return SunColor; }
    float GetSunIntensity() const { return SunIntensity; }

    void SetSunDirection(const FVector& Dir) { SunDirection = Dir.GetSafeNormal(); }
    void SetSunDiskSize(float Size) { SunDiskSize = FMath::Clamp(Size, 0.0f, 1.0f); }
    void SetSunColor(const FLinearColor& Color) { SunColor = Color; }
    void SetSunIntensity(float Intensity) { SunIntensity = FMath::Max(0.0f, Intensity); }

    // === Sphere Parameters ===
    float GetSphereRadius() const { return SphereRadius; }
    void SetSphereRadius(float Radius) { SphereRadius = FMath::Max(100.0f, Radius); }

    // === Shader Access ===
    UShader* GetSkyShader() const { return SkyShader; }

private:
    // Sky Gradient Colors
    UPROPERTY(EditAnywhere, Category="Sky|Colors")
    FLinearColor ZenithColor = FLinearColor(0.0f, 0.1f, 0.4f, 1.0f);  // Deep blue top

    UPROPERTY(EditAnywhere, Category="Sky|Colors")
    FLinearColor HorizonColor = FLinearColor(0.7f, 0.5f, 0.3f, 1.0f);  // Orange/pink horizon

    UPROPERTY(EditAnywhere, Category="Sky|Colors")
    FLinearColor GroundColor = FLinearColor(0.1f, 0.1f, 0.1f, 1.0f);  // Dark ground

    UPROPERTY(EditAnywhere, Category="Sky|Colors", Range="1.0, 10.0")
    float HorizonFalloff = 3.0f;  // Gradient transition sharpness

    UPROPERTY(EditAnywhere, Category="Sky|Colors", Range="0.0, 5.0")
    float OverallBrightness = 1.0f;  // Final brightness multiplier

    // Sun Parameters (Manual Control)
    UPROPERTY(EditAnywhere, Category="Sky|Sun")
    FVector SunDirection = FVector(0.5f, 0.0f, -0.866f);  // Default: 30 degrees above horizon

    UPROPERTY(EditAnywhere, Category="Sky|Sun", Range="0.0, 1.0")
    float SunDiskSize = 0.05f;  // Angular size of sun disk

    UPROPERTY(EditAnywhere, Category="Sky|Sun")
    FLinearColor SunColor = FLinearColor(1.0f, 0.95f, 0.8f, 1.0f);  // Warm white

    UPROPERTY(EditAnywhere, Category="Sky|Sun", Range="0.0, 100.0")
    float SunIntensity = 10.0f;  // Sun brightness

    // Sphere Rendering
    UPROPERTY(EditAnywhere, Category="Sky|Sphere")
    float SphereRadius = 10000.0f;  // Sky sphere radius

    // Shader reference
    UShader* SkyShader = nullptr;
};
