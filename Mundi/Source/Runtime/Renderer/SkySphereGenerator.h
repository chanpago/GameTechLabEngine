#pragma once

#include "VertexData.h"
#include <d3d11.h>

class UStaticMesh;

/**
 * Utility class for generating sky sphere mesh data.
 * Creates an inside-facing sphere mesh for sky rendering.
 */
class FSkySphereGenerator
{
public:
    /**
     * Get or create the cached sky sphere mesh.
     * The mesh is generated once and cached for reuse.
     *
     * @param Device D3D11 device for creating GPU buffers
     * @return Pointer to the cached UStaticMesh, or nullptr on failure
     */
    static UStaticMesh* GetOrCreateSkySphere(ID3D11Device* Device);

    /**
     * Generate sphere mesh vertex and index data.
     * Creates an inside-facing sphere (normals point inward, reversed winding).
     *
     * @param OutVertices Output array for vertex data
     * @param OutIndices Output array for index data
     * @param NumSegments Horizontal divisions (default: 32)
     * @param NumRings Vertical divisions (default: 16)
     * @param Radius Sphere radius (default: 1.0f, scaled at render time)
     */
    static void GenerateSphereMesh(
        TArray<FNormalVertex>& OutVertices,
        TArray<uint32>& OutIndices,
        uint32 NumSegments = 32,
        uint32 NumRings = 16,
        float Radius = 1.0f
    );

private:
    static const FString CacheKey;
};
