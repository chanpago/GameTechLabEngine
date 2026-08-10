#include "pch.h"
#include "AnimNotify_PlaySound.h"
#include "SkeletalMeshComponent.h"
#include "AnimSequenceBase.h" 
#include "Source/Runtime/Engine/GameFramework/FAudioDevice.h"

IMPLEMENT_CLASS(UAnimNotify_PlaySound)
void UAnimNotify_PlaySound::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation)
{
	UE_LOG("PlaySound_Notify");

	if (Sound && MeshComp)
	{
		if (bIs3D)
		{
			FVector SoundPos = MeshComp->GetWorldLocation();
			FAudioDevice::PlaySoundAtLocationOneShot(Sound, SoundPos, Volume);
		}
		else
		{
			FAudioDevice::PlaySound2DOneShot(Sound, Volume);
		}
	}
}
