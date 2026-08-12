#include "pch.h"
#include "PrimitiveComponent.h"
#include "SceneComponent.h"
#include "Actor.h"
#include "WorldPartitionManager.h"
#include "../Physics/BodyInstance.h"


// IMPLEMENT_CLASS is now auto-generated in .generated.cpp
UPrimitiveComponent::UPrimitiveComponent() : bGenerateOverlapEvents(true)
{
	CollisionEnabled = (ECollisionState)CollisionEnabled_Internal;
}

UPrimitiveComponent::~UPrimitiveComponent()
{
    if (BodyInstance)
    {
        delete BodyInstance;
        BodyInstance = nullptr;
    }
}

void UPrimitiveComponent::OnRegister(UWorld* InWorld)
{
    Super::OnRegister(InWorld);
}

void UPrimitiveComponent::OnUnregister()
{
    Super::OnUnregister();
}

void UPrimitiveComponent::SetMaterialByName(uint32 InElementIndex, const FString& InMaterialName)
{
    SetMaterial(InElementIndex, UResourceManager::GetInstance().Load<UMaterial>(InMaterialName));
} 
 
void UPrimitiveComponent::DuplicateSubObjects()
{
    Super::DuplicateSubObjects();
	CollisionEnabled = (ECollisionState)CollisionEnabled_Internal;
}

void UPrimitiveComponent::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
    Super::Serialize(bInIsLoading, InOutHandle);

    if (bInIsLoading)
    {
        FJsonSerializer::ReadBool(InOutHandle, "bOverrideCollisionSetting", bOverrideCollisionSetting, bOverrideCollisionSetting, false);
        FJsonSerializer::ReadInt32(InOutHandle, "CollisionEnabled_Internal", CollisionEnabled_Internal, CollisionEnabled_Internal, false);
        CollisionEnabled = (ECollisionState)CollisionEnabled_Internal;
    }

	if (!bInIsLoading)
	{
        InOutHandle["bOverrideCollisionSetting"] = bOverrideCollisionSetting;
        CollisionEnabled_Internal = (int32)CollisionEnabled;
        InOutHandle["CollisionEnabled_Internal"] = CollisionEnabled_Internal;
	}
}

bool UPrimitiveComponent::IsOverlappingActor(const AActor* Other) const
{
    if (!Other)
    {
        return false;
    }

    const TArray<FOverlapInfo>& Infos = GetOverlapInfos();
    for (const FOverlapInfo& Info : Infos)
    {
        if (Info.Other)
        {
            if (AActor* Owner = Info.Other->GetOwner())
            {
                if (Owner == Other)
                {
                    return true;
                }
            }
        }
    }
    return false;
}
