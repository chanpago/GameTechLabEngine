#include "pch.h"
#include "SkySphereComponent.h"
#include "ResourceManager.h"
#include "BillboardComponent.h"
#include "JsonSerializer.h"

USkySphereComponent::USkySphereComponent()
{
    // Load sky shader
    SkyShader = UResourceManager::GetInstance().Load<UShader>("Shaders/Sky/Sky.hlsl");
}

USkySphereComponent::~USkySphereComponent()
{
}

void USkySphereComponent::OnRegister(UWorld* InWorld)
{
    Super::OnRegister(InWorld);

    // Create editor billboard sprite for visibility in editor
    if (!SpriteComponent && !InWorld->bPie)
    {
        CREATE_EDITOR_COMPONENT(SpriteComponent, UBillboardComponent);
        SpriteComponent->SetTexture(GDataDir + "/UI/Icons/S_AtmosphericHeightFog.dds");
    }
}

void USkySphereComponent::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
    Super::Serialize(bInIsLoading, InOutHandle);

    if (bInIsLoading)
    {
        // Load Sky Colors
        if (InOutHandle.hasKey("ZenithColor"))
        {
            FVector4 ColorVec;
            FJsonSerializer::ReadVector4(InOutHandle, "ZenithColor", ColorVec);
            ZenithColor = FLinearColor(ColorVec);
        }
        if (InOutHandle.hasKey("HorizonColor"))
        {
            FVector4 ColorVec;
            FJsonSerializer::ReadVector4(InOutHandle, "HorizonColor", ColorVec);
            HorizonColor = FLinearColor(ColorVec);
        }
        if (InOutHandle.hasKey("GroundColor"))
        {
            FVector4 ColorVec;
            FJsonSerializer::ReadVector4(InOutHandle, "GroundColor", ColorVec);
            GroundColor = FLinearColor(ColorVec);
        }

        FJsonSerializer::ReadFloat(InOutHandle, "HorizonFalloff", HorizonFalloff, 3.0f);
        FJsonSerializer::ReadFloat(InOutHandle, "OverallBrightness", OverallBrightness, 1.0f);

        // Load Sun Parameters
        if (InOutHandle.hasKey("SunDirection"))
        {
            FJsonSerializer::ReadVector(InOutHandle, "SunDirection", SunDirection);
        }
        FJsonSerializer::ReadFloat(InOutHandle, "SunDiskSize", SunDiskSize, 0.05f);
        if (InOutHandle.hasKey("SunColor"))
        {
            FVector4 ColorVec;
            FJsonSerializer::ReadVector4(InOutHandle, "SunColor", ColorVec);
            SunColor = FLinearColor(ColorVec);
        }
        FJsonSerializer::ReadFloat(InOutHandle, "SunIntensity", SunIntensity, 10.0f);

        // Load Sphere Parameters
        FJsonSerializer::ReadFloat(InOutHandle, "SphereRadius", SphereRadius, 10000.0f);

        // Load Shader
        if (InOutHandle.hasKey("SkyShader"))
        {
            FString ShaderPath = InOutHandle["SkyShader"].ToString();
            if (!ShaderPath.empty())
            {
                SkyShader = UResourceManager::GetInstance().Load<UShader>(ShaderPath.c_str());
            }
        }
    }
    else
    {
        // Save Sky Colors
        InOutHandle["ZenithColor"] = FJsonSerializer::Vector4ToJson(ZenithColor.ToFVector4());
        InOutHandle["HorizonColor"] = FJsonSerializer::Vector4ToJson(HorizonColor.ToFVector4());
        InOutHandle["GroundColor"] = FJsonSerializer::Vector4ToJson(GroundColor.ToFVector4());
        InOutHandle["HorizonFalloff"] = HorizonFalloff;
        InOutHandle["OverallBrightness"] = OverallBrightness;

        // Save Sun Parameters
        InOutHandle["SunDirection"] = FJsonSerializer::VectorToJson(SunDirection);
        InOutHandle["SunDiskSize"] = SunDiskSize;
        InOutHandle["SunColor"] = FJsonSerializer::Vector4ToJson(SunColor.ToFVector4());
        InOutHandle["SunIntensity"] = SunIntensity;

        // Save Sphere Parameters
        InOutHandle["SphereRadius"] = SphereRadius;

        // Save Shader
        if (SkyShader)
        {
            InOutHandle["SkyShader"] = SkyShader->GetFilePath();
        }
        else
        {
            InOutHandle["SkyShader"] = "";
        }
    }
}

void USkySphereComponent::DuplicateSubObjects()
{
    Super::DuplicateSubObjects();
}
