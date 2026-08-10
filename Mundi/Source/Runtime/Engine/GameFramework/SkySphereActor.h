#pragma once

#include "Actor.h"
#include "ASkySphereActor.generated.h"

class USkySphereComponent;

/**
 * Sky Sphere Actor
 * Actor that renders a procedural gradient sky with sun disk.
 */
UCLASS(DisplayName="Sky Sphere", Description="Procedural sky sphere actor with gradient and sun disk")
class ASkySphereActor : public AActor
{
public:
    GENERATED_REFLECTION_BODY()

    ASkySphereActor();

    void DuplicateSubObjects() override;

    /** Get the sky sphere component */
    USkySphereComponent* GetSkyComponent() const { return SkyComponent; }

private:
    USkySphereComponent* SkyComponent = nullptr;
};
