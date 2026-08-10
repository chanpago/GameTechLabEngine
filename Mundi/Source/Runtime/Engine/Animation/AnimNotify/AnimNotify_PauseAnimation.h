#pragma once
#include "AnimNotify.h"

class UAnimNotify_PauseAnimation : public UAnimNotify
{
public:
	DECLARE_CLASS(UAnimNotify_PauseAnimation, UAnimNotify)

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation) override;
};
