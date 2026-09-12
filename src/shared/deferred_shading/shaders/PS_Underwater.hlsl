// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Underwater post-process. Runs after the forward/translucent pass, over the finished scene
// colour, and is skipped entirely while the camera is dry - so out of water this shader does not
// execute at all and every consumer of GetFinalRenderTarget() pays nothing.
//
// Four effects, each independently gated by a uniform so a disabled one costs a scalar branch:
//
//   1. Depth fog and colour tint. Beer-Lambert extinction on the G-Buffer's radial depth, tinted
//      by the liquid's absorption colour. This is the effect that actually reads as "underwater";
//      everything below is garnish on top of it.
//   2. Screen distortion. Two low-frequency warps at different rates, strongest just below the
//      surface.
//   3. Caustics. The caustics texture projected in world space, masked to upward-facing surfaces.
//   4. Sun shafts. A short radial blur from the sun's screen position, masked to shallow depths.
//
// Plus the surface crossing: when the camera plane straddles the waterline the treatment is
// applied only below it, rather than snapping the whole frame.

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
    float2 TexCoord : TEXCOORD0;
};

// The finished scene colour from the forward pass.
Texture2D SceneTexture : register(t0);

// G-Buffer normal target: rgb = worldNormal * 0.5 + 0.5, a = linear RADIAL depth (length(viewPos)).
Texture2D NormalTexture : register(t1);

// Tiling caustics texture. Sampled wrapped, unlike everything else in this shader.
Texture2D CausticsTexture : register(t2);

SamplerState LinearSampler : register(s0);

cbuffer UnderwaterBuffer : register(b2)
{
    // Linear RGB fog colour, and density per world unit in w.
    float4 FogColorAndDensity;

    // Beer-Lambert absorption per channel. w unused.
    float4 AbsorptionColor;

    float2 ScreenSize;
    float2 InvScreenSize;

    // How far the camera is below the surface, in world units.
    float SubmersionDepth;
    // Crossing progress in [0,1]. Scales every effect so breaking the surface does not flash a
    // full-strength filter over a single frame.
    float TransitionPhase;
    // Seconds, for the distortion and caustic scroll.
    float Time;
    float DistortionStrength;

    float CausticsStrength;
    // Non-zero enables the sun shafts.
    float GodRaysEnabled;
    // Sun position in screen UV space. Off-screen values simply produce no shafts.
    float2 SunScreenPos;

    // Screen-space V coordinate of the waterline while the camera straddles the surface.
    // >= 1 means fully submerged and the whole frame is treated.
    float WaterLineV;
    float3 UnderwaterPadding;
};

cbuffer ViewMatrices : register(b12)
{
    column_major matrix matView;
    column_major matrix matProj;
    column_major matrix matInvView;
    column_major matrix InverseProjection;
};

// Reconstructs a view-space position from a screen UV and the RADIAL depth in the normal target's
// alpha. Mirrors PS_ContactShadows.hlsl and PS_DeferredLighting.hlsl so the three cannot disagree.
float3 ReconstructViewPos(float2 uv, float radialDepth)
{
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 viewH = mul(float4(ndc, 1.0f, 1.0f), InverseProjection);
    float3 viewRay = normalize(viewH.xyz / viewH.w);
    return viewRay * radialDepth;
}

float4 main(PS_INPUT input) : SV_TARGET
{
    float2 uv = input.TexCoord;

    // The surface crossing. Above the waterline the frame is left exactly as it came in, which is
    // what makes a half-submerged camera read as half-submerged instead of snapping.
    if (uv.y < WaterLineV)
    {
        return SceneTexture.SampleLevel(LinearSampler, uv, 0);
    }

    // Everything scales by the crossing phase, so entering and leaving the water ramps.
    float strength = saturate(TransitionPhase);

    // --- Distortion ---------------------------------------------------------------------
    // Applied to the lookup UV, so it warps the scene rather than compositing over it. Falls off
    // with depth: the surface churn that causes it is above you, not around you.
    float2 distortedUv = uv;
    if (DistortionStrength > 0.0f)
    {
        float depthFalloff = saturate(1.0f - SubmersionDepth / 12.0f);
        float amplitude = DistortionStrength * strength * depthFalloff * 0.004f;

        distortedUv.x += sin(uv.y * 24.0f + Time * 1.6f) * amplitude;
        distortedUv.y += cos(uv.x * 19.0f + Time * 1.1f) * amplitude * 0.75f;
        distortedUv = clamp(distortedUv, InvScreenSize, 1.0f - InvScreenSize);
    }

    float3 color = SceneTexture.SampleLevel(LinearSampler, distortedUv, 0).rgb;

    float4 normalData = NormalTexture.SampleLevel(LinearSampler, distortedUv, 0);
    float sceneDepth = normalData.a;

    // The sky, and anything else the opaque pass never wrote, reads as depth 0. Seen from
    // underwater that is the brightest thing in frame and must still be fogged, so treat it as
    // maximally distant rather than skipping it.
    bool isSky = sceneDepth <= 0.0f;
    float fogDistance = isSky ? 400.0f : sceneDepth;

    // --- Caustics -----------------------------------------------------------------------
    // Projected in world space so the pattern sticks to the seabed instead of swimming with the
    // camera. Two layers scrolling against each other avoid an obvious repeating loop.
    if (CausticsStrength > 0.0f && !isSky)
    {
        float3 viewPos = ReconstructViewPos(distortedUv, sceneDepth);
        float3 worldPos = mul(float4(viewPos, 1.0f), matInvView).xyz;
        float3 worldNormal = normalize(normalData.rgb * 2.0f - 1.0f);

        // Only surfaces facing up receive light focused through the surface above them.
        float upFacing = saturate(worldNormal.y);

        // Caustics are formed by the surface, so they weaken as the seabed gets further from it.
        float depthFade = saturate(1.0f - SubmersionDepth / 30.0f);

        float2 causticUv0 = worldPos.xz * 0.08f + float2(Time * 0.035f, Time * 0.021f);
        float2 causticUv1 = worldPos.xz * 0.13f - float2(Time * 0.026f, Time * 0.038f);

        float caustic0 = CausticsTexture.SampleLevel(LinearSampler, causticUv0, 0).r;
        float caustic1 = CausticsTexture.SampleLevel(LinearSampler, causticUv1, 0).r;

        // min() of two layers keeps the bright filaments thin and sharp; adding them would wash
        // the whole seabed out.
        float caustic = min(caustic0, caustic1);

        color += caustic * CausticsStrength * upFacing * depthFade * strength;
    }

    // --- Fog and tint -------------------------------------------------------------------
    // Beer-Lambert extinction per channel: red dies first, then green, leaving the deep blue-green
    // that reads as depth. The density term additionally thickens the further down you are.
    float density = FogColorAndDensity.w * (1.0f + SubmersionDepth * 0.02f);

    float3 extinction = exp(-AbsorptionColor.rgb * fogDistance * density);
    color *= lerp(float3(1.0f, 1.0f, 1.0f), extinction, strength);

    float fogFactor = saturate(1.0f - exp(-fogDistance * density));
    color = lerp(color, FogColorAndDensity.rgb, fogFactor * strength);

    // --- Sun shafts ---------------------------------------------------------------------
    // A short radial blur from the sun toward this pixel, accumulating only the bright fragments
    // the scene already has. Masked to shallow water, where light actually still penetrates.
    if (GodRaysEnabled > 0.0f)
    {
        float shaftDepthFade = saturate(1.0f - SubmersionDepth / 25.0f);
        if (shaftDepthFade > 0.0f)
        {
            const int SampleCount = 16;
            float2 delta = (uv - SunScreenPos) / (float)SampleCount * 0.6f;
            float2 sampleUv = uv;
            float decay = 1.0f;
            float3 shaft = float3(0.0f, 0.0f, 0.0f);

            [unroll]
            for (int i = 0; i < SampleCount; ++i)
            {
                sampleUv -= delta;
                float2 clamped = clamp(sampleUv, 0.0f, 1.0f);
                float3 tap = SceneTexture.SampleLevel(LinearSampler, clamped, 0).rgb;

                // Only genuinely bright fragments contribute, so shafts form from the lit surface
                // above rather than smearing the whole scene toward the sun.
                float luminance = dot(tap, float3(0.2126f, 0.7152f, 0.0722f));
                shaft += tap * saturate(luminance - 0.6f) * decay;
                decay *= 0.92f;
            }

            shaft /= (float)SampleCount;
            color += shaft * shaftDepthFade * strength * 1.5f;
        }
    }

    return float4(color, 1.0f);
}
