#include "pch.h"
#include "AnimNotify_ComboWindow.h"
#include "SkeletalMeshComponent.h"
#include "AnimSequenceBase.h"
#include "Actor.h"
#include "PlayerCharacter.h"

IMPLEMENT_CLASS(UAnimNotify_ComboWindow)

UAnimNotify_ComboWindow::UAnimNotify_ComboWindow()
{
    bEnable = true;
}

void UAnimNotify_ComboWindow::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation)
{
    if (!MeshComp)
        return;

    AActor* Owner = MeshComp->GetOwner();
    if (!Owner)
        return;

    // PlayerCharacter로 캐스팅
    APlayerCharacter* Player = Cast<APlayerCharacter>(Owner);
    if (!Player)
    {
        UE_LOG("[AnimNotify_ComboWindow] Owner is not a PlayerCharacter: %s", Owner->GetName().c_str());
        return;
    }

    // 콤보 윈도우 활성화/비활성화
    if (bEnable)
    {
        Player->EnableComboWindow();
    }
    else
    {
        Player->DisableComboWindow();
    }

    UE_LOG("[AnimNotify_ComboWindow] Combo window %s on %s",
           bEnable ? "enabled" : "disabled",
           Owner->GetName().c_str());
}
