#include "pch.h"
#include "AnimNotify_PauseAnimation.h"
#include "SkeletalMeshComponent.h"
#include "AnimInstance.h"

IMPLEMENT_CLASS(UAnimNotify_PauseAnimation)

void UAnimNotify_PauseAnimation::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation)
{
	if (!MeshComp)
	{
		return;
	}

	UAnimInstance* AnimInst = MeshComp->GetAnimInstance();
	if (AnimInst)
	{
		// 애니메이션 일시정지 (마지막 포즈 유지)
		AnimInst->bPaused = true;
		UE_LOG("[AnimNotify_PauseAnimation] Animation paused");
	}
}
