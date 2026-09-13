// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Underwater post-process. Runs after the forward/translucent pass, over the finished scene
// colour, and is skipped entirely while the camera is dry - so out of water this shader does not
// execute at all and every consumer of GetFinalRenderTarget() pays nothing.
//
// Four effects, each independently gated by a uniform so a disabled one costs a scalar branch:
//
//   1. Depth fog and colour tint. Beer-Lambert extinction over the distance each view ray actually
//      travels through water, tinted by the liquid's absorption colour. This is the effect that
//      actually reads as "underwater"; everything below is garnish on top of it.
//   2. Screen distortion. Two low-frequency warps at different rates, strongest just below the
//      surface.
//   3. Caustics. The caustics texture projected in world space, masked to upward-facing surfaces
//      below the waterline.
//   4. Sun shafts. A short radial blur from the sun's screen position, masked to shallow depths.

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

// Tiling caustics texture. Read through SampleWrappedRed, never through LinearSampler.
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

    // World Y of the water surface above the camera.
    float SurfaceHeight;
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

// Bilinear fetch of the red channel that wraps. The pass sets a clamped sampler, because every
// other texture here is a screen-sized target, and a clamped lookup smears a tiling texture into
// edge-texel streaks as soon as the UV leaves [0,1] - which for a world-space projection is
// everywhere but one small patch of the map.
float SampleWrappedRed(Texture2D tex, float2 uv)
{
    uint width;
    uint height;
    tex.GetDimensions(width, height);

    int2 size = int2((int)width, (int)height);

    // Wrap before scaling to texels, so a position far from the world origin does not cost the
    // bilinear weights their precision.
    float2 texel = frac(uv) * float2(size) - 0.5f;
    float2 cell = floor(texel);
    float2 blend = texel - cell;

    // After frac() the cell is always in [-1, size - 1], so a single fold at each end wraps it
    // without integer modulus, which is slow on the GPU.
    int2 p0 = (int2)cell;
    p0 += size * (int2)(p0 < 0);
    int2 p1 = p0 + 1;
    p1 -= size * (int2)(p1 >= size);

    float s00 = tex.Load(int3(p0.x, p0.y, 0)).r;
    float s10 = tex.Load(int3(p1.x, p0.y, 0)).r;
    float s01 = tex.Load(int3(p0.x, p1.y, 0)).r;
    float s11 = tex.Load(int3(p1.x, p1.y, 0)).r;

    return lerp(lerp(s00, s10, blend.x), lerp(s01, s11, blend.x), blend.y);
}

float4 main(PS_INPUT input) : SV_TARGET
{
    float2 uv = input.TexCoord;

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

    // Point-fetched, never filtered. Depth does not interpolate across a silhouette: halfway between
    // a tree 150 units away and the sky (stored as 0) is not an object 75 units away, and such an
    // in-between value can land close enough to the camera to escape the fog almost entirely -
    // a speckled bright outline around every tree and ridge on the horizon.
    int2 depthPixel = clamp((int2)(distortedUv * ScreenSize), int2(0, 0), (int2)ScreenSize - 1);
    float4 normalData = NormalTexture.Load(int3(depthPixel, 0));
    float sceneDepth = normalData.a;

    // The sky, and anything else the opaque pass never wrote, reads as depth 0.
    bool isSky = sceneDepth <= 0.0f;

    // --- Water path length --------------------------------------------------------------
    // Fog only the part of each view ray that is actually under water. Fogging by the full scene
    // depth instead treats everything seen up through the surface - the sky, a tree on the shore -
    // as if it lay hundreds of units deep, and the surface overhead turns into a flat wall of fog
    // colour instead of a bright ceiling that fades out with distance.
    float3 cameraPos = mul(float4(0.0f, 0.0f, 0.0f, 1.0f), matInvView).xyz;
    float3 viewRay = ReconstructViewPos(distortedUv, 1.0f);
    float3 rayDir = normalize(mul(float4(viewRay, 0.0f), matInvView).xyz);

    // Sky seen sideways or downward never leaves the water, so it still has to fade out fully.
    float sceneDistance = isSky ? 400.0f : sceneDepth;

    // Clamped to the surface: while surfacing the camera is already above the water but the
    // crossing is still ramping out, and measuring from above the surface would count air as water.
    float eyeDepth = max(SurfaceHeight - cameraPos.y, 0.0f);

    float fogDistance = sceneDistance;
    if (rayDir.y > 0.0001f)
    {
        fogDistance = min(sceneDistance, eyeDepth / rayDir.y);
    }

    // --- Snell's window -----------------------------------------------------------------
    // Seen from below, the surface only lets through light arriving within the critical angle,
    // about 48.6 degrees from vertical. Beyond it the surface is a mirror showing the water itself.
    // Without this the shore, the trees and the horizon above the water stay visible at grazing
    // angles as thin, heavily fogged strips: tinted bands across the upper half of the frame.
    if (rayDir.y > 0.0001f && eyeDepth / rayDir.y < sceneDistance)
    {
        // cos(48.6 degrees) = 0.661. The smoothstep keeps the rim of the window soft.
        float window = smoothstep(0.60f, 0.72f, rayDir.y);

        // Stylized total internal reflection: the water's own colour, brightening toward the rim
        // of the window so the ceiling reads as a gradient rather than a flat lid.
        float3 internalReflection = FogColorAndDensity.rgb * lerp(0.75f, 1.15f, saturate(rayDir.y / 0.66f));

        color = lerp(color, lerp(internalReflection, color, window), strength);
    }

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

        // Caustics are focused by the surface above the lit point, so they fade with that point's
        // own depth - not the camera's - and do not exist at all above the waterline.
        float pointDepth = SurfaceHeight - worldPos.y;
        float depthFade = saturate(1.0f - pointDepth / 30.0f) * step(0.0f, pointDepth);

        float2 causticUv0 = worldPos.xz * 0.08f + float2(Time * 0.035f, Time * 0.021f);
        float2 causticUv1 = worldPos.xz * 0.13f - float2(Time * 0.026f, Time * 0.038f);

        float caustic0 = SampleWrappedRed(CausticsTexture, causticUv0);
        float caustic1 = SampleWrappedRed(CausticsTexture, causticUv1);

        // min() of two layers keeps the bright filaments thin and sharp; adding them would wash
        // the whole seabed out.
        float caustic = min(caustic0, caustic1);

        // Scales the light already on the surface rather than adding a fixed amount, so the
        // pattern follows the time of day and a seabed at night does not glow.
        color *= 1.0f + caustic * CausticsStrength * 2.0f * upFacing * depthFade * strength;
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
