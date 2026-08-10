#include "pch.h"
#include "AnimNotify_PlayCamera.h"

#include "Source/Runtime/Engine/GameFramework/PlayerCameraManager.h"
#include "Source/Runtime/Engine/GameFramework/World.h"
#include "Source/Runtime/Engine/Components/SkeletalMeshComponent.h"


IMPLEMENT_CLASS(UAnimNotify_PlayCamera)

void UAnimNotify_PlayCamera::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation)
{
    if (!MeshComp)
    {
        return;
    }

    UWorld* World = MeshComp->GetWorld();
    if (!World)
    {
        return;
    }

    APlayerCameraManager* CameraManager = World->GetPlayerCameraManager();

    switch (EffectType)
    {
    case ECameraNotifyEffect::Shake:
        if (!CameraManager)
        {
            return;
        }
        CameraManager->StartCameraShake(
            ShakeSettings.Duration,
            ShakeSettings.AmplitudeLocation,
            ShakeSettings.AmplitudeRotationDeg,
            ShakeSettings.Frequency,
            ShakeSettings.Priority);
        break;
    case ECameraNotifyEffect::Fade:
        if (!CameraManager) { return; }
        CameraManager->StartFade(
            FadeSettings.Duration,
            FadeSettings.FromAlpha,
            FadeSettings.ToAlpha,
            FadeSettings.Color,
            FadeSettings.Priority);
        break;
    case ECameraNotifyEffect::LetterBox:
        if (!CameraManager) { return; }
        CameraManager->StartLetterBox(
            LetterBoxSettings.Duration,
            LetterBoxSettings.Aspect,
            LetterBoxSettings.BarHeight,
            LetterBoxSettings.Color,
            LetterBoxSettings.Priority);
        break;
    case ECameraNotifyEffect::Vignette:
        if (!CameraManager) { return; }
        CameraManager->StartVignette(
            VignetteSettings.Duration,
            VignetteSettings.Radius,
            VignetteSettings.Softness,
            VignetteSettings.Intensity,
            VignetteSettings.Roundness,
            VignetteSettings.Color,
            VignetteSettings.Priority);
        break;
    case ECameraNotifyEffect::Gamma:
        if (!CameraManager) { return; }
        CameraManager->StartGamma(GammaSettings.Gamma);
        break;
    case ECameraNotifyEffect::DOF:
        if (!CameraManager) { return; }
        CameraManager->StartDOF(
            DOFSettings.FocalDistance,
            DOFSettings.FocalRegion,
            DOFSettings.NearTransition,
            DOFSettings.FarTransition,
            DOFSettings.MaxNearBlur,
            DOFSettings.MaxFarBlur,
            DOFSettings.Priority,
            DOFSettings.Duration);
        break;
    case ECameraNotifyEffect::HitStop:
        World->RequestHitStop(HitStopSettings.Duration, HitStopSettings.Dilation);
        break;
    case ECameraNotifyEffect::Slomo:
        World->RequestSlomo(SlomoSettings.Duration, SlomoSettings.Dilation);
        break;
    default:
        break;
    }
}
