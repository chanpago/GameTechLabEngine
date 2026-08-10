#include "pch.h"
#include "SkySphereGenerator.h"
#include "StaticMesh.h"
#include "ResourceManager.h"
#include "D3D11RHI.h"

const FString FSkySphereGenerator::CacheKey = "Internal/SkySphere";

UStaticMesh* FSkySphereGenerator::GetOrCreateSkySphere(ID3D11Device* Device)
{
    if (!Device)
    {
        return nullptr;
    }

    // Check if already cached
    UStaticMesh* CachedMesh = UResourceManager::GetInstance().Get<UStaticMesh>(CacheKey);
    if (CachedMesh)
    {
        return CachedMesh;
    }

    // Generate mesh data (reduced tessellation: 24x12 is sufficient for smooth gradients)
    TArray<FNormalVertex> Vertices;
    TArray<uint32> Indices;
    GenerateSphereMesh(Vertices, Indices, 24, 12, 1.0f);

    if (Vertices.empty() || Indices.empty())
    {
        return nullptr;
    }

    // Create static mesh asset data
    FStaticMesh* SphereAsset = new FStaticMesh();
    SphereAsset->PathFileName = CacheKey;
    SphereAsset->Vertices = std::move(Vertices);
    SphereAsset->Indices = std::move(Indices);
    SphereAsset->bHasMaterial = false;

    // Create single group info covering all indices
    FGroupInfo GroupInfo;
    GroupInfo.StartIndex = 0;
    GroupInfo.IndexCount = static_cast<uint32>(SphereAsset->Indices.size());
    GroupInfo.InitialMaterialName = "";
    SphereAsset->GroupInfos.push_back(GroupInfo);

    // Create UStaticMesh and load the asset data
    UStaticMesh* SkySphere = ObjectFactory::NewObject<UStaticMesh>();
    SkySphere->SetStaticMeshAsset(SphereAsset);

    // Use Load(FMeshData*) to create GPU buffers once

    // Convert to FMeshData format for Load()
    FMeshData MeshData;
    MeshData.Vertices.reserve(SphereAsset->Vertices.size());
    MeshData.Normal.reserve(SphereAsset->Vertices.size());
    MeshData.UV.reserve(SphereAsset->Vertices.size());
    MeshData.Color.reserve(SphereAsset->Vertices.size());

    for (const auto& Vtx : SphereAsset->Vertices)
    {
        MeshData.Vertices.push_back(Vtx.pos);
        MeshData.Normal.push_back(Vtx.normal);
        MeshData.UV.push_back(Vtx.tex);
        MeshData.Color.push_back(Vtx.color);
    }
    MeshData.Indices = SphereAsset->Indices;

    // Use Load with FMeshData
    SkySphere->Load(&MeshData, Device, EVertexLayoutType::PositionColorTexturNormal);
    SkySphere->SetStaticMeshAsset(SphereAsset);

    // Cache the mesh
    UResourceManager::GetInstance().Add<UStaticMesh>(CacheKey, SkySphere);

    return SkySphere;
}

void FSkySphereGenerator::GenerateSphereMesh(
    TArray<FNormalVertex>& OutVertices,
    TArray<uint32>& OutIndices,
    uint32 NumSegments,
    uint32 NumRings,
    float Radius)
{
    OutVertices.clear();
    OutIndices.clear();

    const float PI = 3.14159265358979323846f;

    // Generate vertices
    // We need (NumSegments + 1) vertices per ring to handle UV wrapping
    // And (NumRings + 1) rings from pole to pole
    for (uint32 Ring = 0; Ring <= NumRings; ++Ring)
    {
        // Phi goes from 0 (top/north pole) to PI (bottom/south pole)
        float Phi = PI * static_cast<float>(Ring) / static_cast<float>(NumRings);
        float SinPhi = sinf(Phi);
        float CosPhi = cosf(Phi);

        for (uint32 Seg = 0; Seg <= NumSegments; ++Seg)
        {
            // Theta goes from 0 to 2*PI around the sphere
            float Theta = 2.0f * PI * static_cast<float>(Seg) / static_cast<float>(NumSegments);
            float SinTheta = sinf(Theta);
            float CosTheta = cosf(Theta);

            FNormalVertex Vertex;

            // Position on unit sphere
            // X = sin(phi) * cos(theta)
            // Y = sin(phi) * sin(theta)
            // Z = cos(phi)  (Z is up)
            Vertex.pos = FVector(
                SinPhi * CosTheta,
                SinPhi * SinTheta,
                CosPhi
            ) * Radius;

            // Normal pointing INWARD for inside-facing sphere
            // This is the negative of the outward-facing normal
            Vertex.normal = -Vertex.pos.GetSafeNormal();

            // UV coordinates
            // U goes 0->1 around the sphere (theta direction)
            // V goes 0->1 from top to bottom (phi direction)
            Vertex.tex = FVector2D(
                static_cast<float>(Seg) / static_cast<float>(NumSegments),
                static_cast<float>(Ring) / static_cast<float>(NumRings)
            );

            // Tangent (not used for sky, but fill with reasonable value)
            FVector Tangent = FVector(-SinTheta, CosTheta, 0.0f);
            Vertex.Tangent = FVector4(Tangent.X, Tangent.Y, Tangent.Z, 1.0f);

            // Color (white)
            Vertex.color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);

            OutVertices.push_back(Vertex);
        }
    }

    // Generate indices with REVERSED winding order for inside-facing
    // Standard CCW winding would be: current, next, current+row
    // For inside-facing (CW from inside), we reverse: current, current+row, next
    for (uint32 Ring = 0; Ring < NumRings; ++Ring)
    {
        for (uint32 Seg = 0; Seg < NumSegments; ++Seg)
        {
            uint32 Current = Ring * (NumSegments + 1) + Seg;
            uint32 Next = Current + 1;
            uint32 CurrentNextRow = Current + (NumSegments + 1);
            uint32 NextNextRow = Next + (NumSegments + 1);

            // First triangle (reversed winding for inside-facing)
            // Original CCW: Current, Next, CurrentNextRow
            // Reversed CW:  Current, CurrentNextRow, Next
            OutIndices.push_back(Current);
            OutIndices.push_back(CurrentNextRow);
            OutIndices.push_back(Next);

            // Second triangle (reversed winding for inside-facing)
            // Original CCW: Next, CurrentNextRow, NextNextRow
            // Reversed CW:  Next, CurrentNextRow, NextNextRow -> Next, NextNextRow, CurrentNextRow
            OutIndices.push_back(Next);
            OutIndices.push_back(CurrentNextRow);
            OutIndices.push_back(NextNextRow);
        }
    }
}
