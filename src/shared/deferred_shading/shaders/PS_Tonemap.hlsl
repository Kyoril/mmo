// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Final pass of the deferred frame: adds bloom to the linear HDR scene, applies exposure, ACES and
// gamma, applies the zone colour grade and LUTs, and dithers. Everything earlier in the frame must
// stay linear.

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
    float2 TexCoord : TEXCOORD0;
};

Texture2D SceneTexture : register(t0);
Texture2D BloomTexture : register(t1);
Texture2D LutTexture : register(t2);
Texture2D LutFromTexture : register(t3);
SamplerState LinearSampler : register(s0);
SamplerState LutSampler : register(s4);

cbuffer TonemapBuffer : register(b2)
{
    float Exposure;
    float BloomScale;       // bloom intensity already divided by the level count; 0 without bloom
    float DitherStrength;   // in 8-bit steps
    float Saturation;       // 0 = grey, 1 = unchanged
    float3 ColorFilter;     // multiplies the graded colour
    float Contrast;         // around mid-grey, 1 = unchanged
    float LutBlend;         // weight of LutTexture; LutFromTexture gets 1 - LutBlend
    float LutSize;          // edge length of LutTexture, 0 = none
    float LutFromSize;      // edge length of LutFromTexture, 0 = none
    float _GradingPadding;
};

// Strip LUT lookup (N*N x N; blue = slice, red = column, green = row). Mirrors
// deferred_shading/color_grading.h - change both together. Size 0 passes the colour through.
float3 SampleStripLut(Texture2D lut, float size, float3 color)
{
    if (size < 2.0f)
    {
        return color;
    }

    float3 c = saturate(color);
    float scale = size - 1.0f;
    float slice = c.b * scale;
    float slice0 = floor(slice);
    float slice1 = min(slice0 + 1.0f, scale);
    float column = c.r * scale + 0.5f;
    float width = size * size;
    float v = (c.g * scale + 0.5f) / size;

    float3 lower = lut.SampleLevel(LutSampler, float2((slice0 * size + column) / width, v), 0.0f).rgb;
    float3 upper = lut.SampleLevel(LutSampler, float2((slice1 * size + column) / width, v), 0.0f).rgb;
    return lerp(lower, upper, slice - slice0);
}

float3 ACESFilm(float3 x)
{
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float InterleavedGradientNoise(float2 position)
{
    return frac(52.9829189f * frac(dot(position, float2(0.06711056f, 0.00583715f))));
}

float3 Sanitise(float3 c)
{
    // Lighting can leave NaN/Inf in the linear HDR scene (GGX 0/0, values above the RGBA16F
    // range); ACES on Inf/NaN would otherwise turn the whole pixel NaN in the final image.
    // FXC compiles without IEEE strictness, which folds isnan/isinf to a constant false, so the
    // exponent bits are tested directly instead: NaN and Inf both set all exponent bits (0x7f800000).
    return (any((asuint(c) & 0x7fffffffu) >= 0x7f800000u)) ? float3(0.0f, 0.0f, 0.0f) : min(c, 1e4f);
}

float4 main(PS_INPUT input) : SV_TARGET
{
    float4 scene = SceneTexture.Load(int3(input.Position.xy, 0));
    float3 bloom = BloomTexture.SampleLevel(LinearSampler, input.TexCoord, 0).rgb;

    float3 hdr = min(Sanitise(scene.rgb) + Sanitise(bloom) * BloomScale, 1e4f);
    float3 color = pow(ACESFilm(hdr * Exposure), 1.0f / 2.2f);

    // Colour grading in display space: filter, saturation, contrast, then the zone LUTs.
    color *= ColorFilter;
    float luma = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    color = lerp(luma.xxx, color, Saturation);
    color = saturate((color - 0.5f) * Contrast + 0.5f);
    color = lerp(SampleStripLut(LutFromTexture, LutFromSize, color), SampleStripLut(LutTexture, LutSize, color), LutBlend);

    // Triangular noise in [-1, 1]: two uniform samples summed. Breaks the 8-bit banding the fog
    // gradients would otherwise show once the frame lands in the back buffer.
    float noise = InterleavedGradientNoise(input.Position.xy) + InterleavedGradientNoise(input.Position.xy + 17.0f) - 1.0f;
    color += noise * (DitherStrength / 255.0f);

    // Alpha passes through: the world frame quad has always received the lit scene's alpha.
    return float4(color, scene.a);
}
