#pragma once
#include "CameraModifierBase.h"
#include "PostProcessing/PostProcessing.h"

class UCamMod_Vignette : public UCameraModifierBase
{

public:
    DECLARE_CLASS(UCamMod_Vignette, UCameraModifierBase)

    UCamMod_Vignette() = default;
    virtual ~UCamMod_Vignette() = default;
    FLinearColor Color = FLinearColor::Zero();
    float Radius = 0.5f;    // 반지름
    float Softness = 0.5f;  // 안쪽의 부드러운 정도
    float Intensity = 1.0f; // 색깔 세기 (목표 값)
    float Roundness = 2.0f; // 1은 마름모, 2는 원, 큰 값으면 사각형

    // Fade in/out interpolation
    float FadeInTime = 0.0f;   // 페이드 인 시간 (0이면 즉시)
    float FadeOutTime = 0.0f;  // 페이드 아웃 시간 (0이면 즉시)
    float CurrentIntensity = 0.0f; // 현재 보간된 Intensity 값

    virtual void ApplyToView(float DeltaTime, FMinimalViewInfo* ViewInfo) override
    {
        if (!bEnabled) return;

        // Duration is total time: FadeIn + Hold + FadeOut
        // Hold time = Duration - FadeInTime - FadeOutTime
        const float HoldTime = FMath::Max(0.0f, Duration - FadeInTime - FadeOutTime);
        const float FadeOutStart = FadeInTime + HoldTime;

        if (Elapsed < FadeInTime)
        {
            // Fade In phase
            const float T = (FadeInTime <= 0.0f) ? 1.0f : FMath::Clamp(Elapsed / FadeInTime, 0.0f, 1.0f);
            CurrentIntensity = FMath::Lerp(0.0f, Intensity, T);
        }
        else if (Elapsed < FadeOutStart)
        {
            // Hold phase - full intensity
            CurrentIntensity = Intensity;
        }
        else
        {
            // Fade Out phase
            const float FadeOutElapsed = Elapsed - FadeOutStart;
            const float T = (FadeOutTime <= 0.0f) ? 1.0f : FMath::Clamp(FadeOutElapsed / FadeOutTime, 0.0f, 1.0f);
            CurrentIntensity = FMath::Lerp(Intensity, 0.0f, T);
        }

        Elapsed += DeltaTime;
    }

    virtual void TickLifetime(float DeltaTime) override
    {
        // Disable when fully faded out and duration complete
        if (Duration >= 0.0f && Elapsed >= Duration)
        {
            bEnabled = false;
        }
    }

    virtual void CollectPostProcess(TArray<FPostProcessModifier>& Out) override
    {
        if (!bEnabled) return;
        if (CurrentIntensity <= 0.0f) return; // Don't render if fully transparent

        FPostProcessModifier M;
        M.Type = EPostProcessEffectType::Vignette;
        M.Priority = Priority;
        M.bEnabled = true;
        M.Weight = Weight;
        M.SourceObject = this;

        M.Payload.Color = Color;

        M.Payload.Params0 = FVector4(Radius, Softness, CurrentIntensity, Roundness);

        Out.Add(M);
    }
};

