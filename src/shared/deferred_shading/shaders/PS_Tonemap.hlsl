// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Final pass of the deferred frame: adds bloom to the linear HDR scene, applies exposure, ACES and
// gamma, and dithers. Everything earlier in the frame must stay linear.

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
    float2 TexCoord : TEXCOORD0;
};

Texture2D SceneTexture : register(t0);
Texture2D BloomTexture : register(t1);
SamplerState LinearSampler : register(s0);

cbuffer TonemapBuffer : register(b2)
{
    float Exposure;
    float BloomScale;       // bloom intensity already divided by the level count; 0 without bloom
    float DitherStrength;   // in 8-bit steps
    float _TonemapPadding;
};

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

float4 main(PS_INPUT input) : SV_TARGET
{
    float4 scene = SceneTexture.Load(int3(input.Position.xy, 0));
    float3 bloom = BloomTexture.SampleLevel(LinearSampler, input.TexCoord, 0).rgb;

    float3 hdr = scene.rgb + bloom * BloomScale;
    float3 color = pow(ACESFilm(hdr * Exposure), 1.0f / 2.2f);

    // Triangular noise in [-1, 1]: two uniform samples summed. Breaks the 8-bit banding the fog
    // gradients would otherwise show once the frame lands in the back buffer.
    float noise = InterleavedGradientNoise(input.Position.xy) + InterleavedGradientNoise(input.Position.xy + 17.0f) - 1.0f;
    color += noise * (DitherStrength / 255.0f);

    // Alpha passes through: the world frame quad has always received the lit scene's alpha.
    return float4(color, scene.a);
}
