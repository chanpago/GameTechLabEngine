#include "pch.h"
#include "AnimNotify_ParticleOnOff.h"
#include "Source/Runtime/Engine/Components/SkeletalMeshComponent.h"
#include "Source/Runtime/Engine/Components/ParticleSystemComponent.h"
#include "Source/Runtime/Core/Object/Actor.h"

IMPLEMENT_CLASS(UAnimNotify_ParticleOnOff)

UAnimNotify_ParticleOnOff::UAnimNotify_ParticleOnOff()
{
	bActivate = true;
}

void UAnimNotify_ParticleOnOff::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation)
{
	Super::Notify(MeshComp, Animation);

	if (!MeshComp)
	{
		return;
	}

	AActor* Owner = MeshComp->GetOwner();
	if (!Owner)
	{
		return;
	}

	bool bAll = ParticleName.empty() || ParticleName == "All";

	for (UActorComponent* Component : Owner->GetOwnedComponents())
	{
		if (UParticleSystemComponent* ParticleComp = Cast<UParticleSystemComponent>(Component))
		{
			bool bMatch = false;
			if (bAll)
			{
				bMatch = true;
			}
			else
			{
				FString CompName = ParticleComp->GetParticleName();
				if (CompName.empty())
				{
					CompName = ParticleComp->GetName();
				}

				if (CompName.find(ParticleName) != FString::npos)
				{
					bMatch = true;
				}
			}

			if (bMatch)
			{
				if (bActivate)
				{
					ParticleComp->ResumeSpawning();
				}
				else
				{
					ParticleComp->PauseSpawning();
				}
			}
		}
	}
}
