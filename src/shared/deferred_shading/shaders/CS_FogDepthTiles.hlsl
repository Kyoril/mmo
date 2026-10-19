// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Farthest scene depth visible through each fog grid column: the maximum linear radial depth over the
// column's screen tile, sky counted as infinitely far. CS_FogInject skips the sun shadow taps of cells that
// start behind it (radial depth bounds the view depth from above, so the bound is conservative).
// Output: one slice, r = far depth in metres.

#include "VolumetricFogCommon.hlsli"

Texture2D GBufferNormal : register(t0);   // a = linear radial depth, 0 = sky
RWTexture3D<float4> FarDepthOutput : register(u0);

// Stored for sky pixels; above any depth the fog grid can reach and still a finite half float.
static const float SKY_DEPTH = 60000.0f;

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= GridWidth || id.y >= GridHeight)
    {
        return;
    }

    float2 renderSize;
    GBufferNormal.GetDimensions(renderSize.x, renderSize.y);
    float2 pixelsPerColumn = renderSize / float2(GridWidth, GridHeight);

    int2 firstPixel = int2(floor(float2(id.xy) * pixelsPerColumn));
    int2 endPixel = min(int2(ceil(float2(id.xy + 1) * pixelsPerColumn)), int2(renderSize));

    float farDepth = 0.0f;
    for (int y = firstPixel.y; y < endPixel.y; ++y)
    {
        for (int x = firstPixel.x; x < endPixel.x; ++x)
        {
            float radialDepth = GBufferNormal.Load(int3(x, y, 0)).a;
            farDepth = max(farDepth, radialDepth <= 0.0f ? SKY_DEPTH : radialDepth);
        }
    }

    FarDepthOutput[uint3(id.xy, 0)] = float4(farDepth, 0.0f, 0.0f, 0.0f);
}
