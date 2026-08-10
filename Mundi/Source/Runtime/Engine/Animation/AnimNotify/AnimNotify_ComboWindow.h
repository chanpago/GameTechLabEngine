#pragma once
#include "AnimNotify.h"

// ============================================================================
// UAnimNotify_ComboWindow - 콤보 윈도우 활성화/비활성화 노티파이
// ============================================================================
class UAnimNotify_ComboWindow : public UAnimNotify
{
public:
    DECLARE_CLASS(UAnimNotify_ComboWindow, UAnimNotify)

    UAnimNotify_ComboWindow();

    virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation) override;

    // 콤보 윈도우 활성화 여부
    UPROPERTY(EditAnywhere, Category = "Combo")
    bool bEnable = true;
};
