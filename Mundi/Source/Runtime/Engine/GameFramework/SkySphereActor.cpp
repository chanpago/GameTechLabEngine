#include "pch.h"
#include "SkySphereActor.h"
#include "SkySphereComponent.h"

ASkySphereActor::ASkySphereActor()
{
    ObjectName = "Sky Sphere Actor";
    SkyComponent = CreateDefaultSubobject<USkySphereComponent>("SkySphereComponent");
    RootComponent = SkyComponent;
}

void ASkySphereActor::DuplicateSubObjects()
{
    Super::DuplicateSubObjects();

    // Find the duplicated sky component
    for (UActorComponent* Component : OwnedComponents)
    {
        if (USkySphereComponent* Sky = Cast<USkySphereComponent>(Component))
        {
            SkyComponent = Sky;
            break;
        }
    }
}
