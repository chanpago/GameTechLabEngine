#pragma once
#include "AnimNotify.h"
#include "Source/Runtime/Engine/Audio/Sound.h"

class UAnimNotify_PlaySound : public UAnimNotify
{
public:
	DECLARE_CLASS(UAnimNotify_PlaySound, UAnimNotify)

	USound* Sound = nullptr;
	float Volume = 1.0f;  // Amplitude/loudness (0.0 to 3.0)
	bool bIs3D = false;   // Use 3D spatial audio (position-based panning). Disable for UI/music.

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation) override;

};
