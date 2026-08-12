// Copies the current viewport's device-Z into HZB mip 0.

cbuffer HZBInitConstants : register(b0)
{
    uint ViewportStartX;
    uint ViewportStartY;
    uint ViewportWidth;
    uint ViewportHeight;
};

Texture2D<float> SceneDepth : register(t0);
RWTexture2D<float> HZBOutput : register(u0);

[numthreads(8, 8, 1)]
void mainCS(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    const uint2 Pixel = DispatchThreadId.xy;
    if (Pixel.x >= ViewportWidth || Pixel.y >= ViewportHeight)
    {
        return;
    }

    HZBOutput[Pixel] = SceneDepth.Load(int3(Pixel + uint2(ViewportStartX, ViewportStartY), 0));
}
