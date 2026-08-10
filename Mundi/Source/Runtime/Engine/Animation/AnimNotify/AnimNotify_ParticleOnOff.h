#pragma once
#include "AnimNotify.h"

UCLASS(DisplayName = "파티클 On/Off")
class UAnimNotify_ParticleOnOff : public UAnimNotify
{
public:
	DECLARE_CLASS(UAnimNotify_ParticleOnOff, UAnimNotify);

	UAnimNotify_ParticleOnOff();

	void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation) override;

	UPROPERTY(EditAnywhere, Category = "Notify")
	bool bActivate = true;

	UPROPERTY(EditAnywhere, Category = "Notify")
	FString ParticleName; // 비어있으면 "All"
};
