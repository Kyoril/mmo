// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Records the sky as it is drawn on screen into a small azimuth x elevation map that the ocean
// reflects wherever the reflected ray leaves the screen. The drawn sky is mostly fog and sun haze
// over the authored gradient (a hazy dawn is cream although its authored colours are orange), which
// no closed form reproduces, so the water copies what was actually drawn instead.
//
// One thread per azimuth column walks the elevations from the horizon up. A texel whose direction
// is on screen and hits sky takes the drawn colour (blended over a few frames); one that is not
// keeps what it last saw, and one that never saw sky repeats the highest sky seen below it in its
// column. a = 1 once the texel holds sky, 0 while the column has seen none (the shader then falls
// back to the authored gradient).

cbuffer OceanSkyCaptureConstants : register(b0)
{
    column_major matrix ViewProj;
    float3 CameraPosition;
    float BlendWeight;       // share of a new observation per frame; 1 = take it outright
    float MaxRadiance;       // clamp per sample, keeps the sun disc out of a whole texel
    float3 _Padding;
};

Texture2DArray<float4> PreviousSky : register(t0);
Texture2D SceneColor : register(t1);
Texture2D SceneDepth : register(t2);   // G-buffer normal target: radial depth in alpha, 0 = sky
RWTexture2DArray<float4> CapturedSky : register(u0);

#define SKY_MAP_WIDTH 64
#define SKY_MAP_HEIGHT 16

static const float SKY_PI = 3.14159265f;

// Direction of a sky map texel. Rows are spaced by the square root of the height above the horizon,
// so most of them sit low, where reflections of the sea look.
float3 SkyMapDirection(float2 texel)
{
    float azimuth = (texel.x + 0.5f) / SKY_MAP_WIDTH * 2.0f * SKY_PI - SKY_PI;
    float row = (texel.y + 0.5f) / SKY_MAP_HEIGHT;
    float up = row * row;
    float horizontal = sqrt(max(1.0f - up * up, 0.0f));
    return float3(cos(azimuth) * horizontal, up, sin(azimuth) * horizontal);
}

// The drawn sky in a direction: rgb and whether it is on screen and sky at all (a).
float4 ObserveSky(float3 direction)
{
    float4 clipPos = mul(float4(CameraPosition + direction * 20000.0f, 1.0f), ViewProj);
    if (clipPos.w <= 1e-4f)
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    float2 ndc = clipPos.xy / clipPos.w;
    if (abs(ndc.x) > 0.98f || abs(ndc.y) > 0.98f)
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    uint width, height;
    SceneColor.GetDimensions(width, height);
    int2 pixel = int2(float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f) * float2(width - 1, height - 1));
    if (SceneDepth.Load(int3(pixel, 0)).a > 0.0f)
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    float3 color = SceneColor.Load(int3(pixel, 0)).rgb;
    if (any((asuint(color) & 0x7fffffffu) >= 0x7f800000u))
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    float peak = max(max(color.r, color.g), max(color.b, 1e-4f));
    return float4(color * min(1.0f, MaxRadiance / peak), 1.0f);
}

[numthreads(SKY_MAP_WIDTH, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    float4 carried = float4(0.0f, 0.0f, 0.0f, 0.0f);
    for (uint row = 0; row < SKY_MAP_HEIGHT; ++row)
    {
        uint3 texel = uint3(id.x, row, 0);
        float4 previous = PreviousSky.Load(int4(texel, 0));
        float4 observed = ObserveSky(SkyMapDirection(float2(texel.xy)));

        float4 result;
        if (observed.a > 0.0f)
        {
            float weight = previous.a > 0.0f ? BlendWeight : 1.0f;
            result = float4(lerp(previous.rgb, observed.rgb, weight), 1.0f);
        }
        else if (previous.a > 0.0f)
        {
            result = previous;
        }
        else
        {
            result = carried;
        }

        CapturedSky[texel] = result;
        if (result.a > 0.0f)
        {
            carried = result;
        }
    }
}
