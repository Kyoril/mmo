// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Screen-space contact shadows for the sun. Marches a short ray toward the light through the
// G-Buffer's depth and writes a single-channel occlusion term the lighting pass multiplies into the
// directional light's shadow.
//
// Why this exists: a cascaded shadow map has finite resolution and needs a depth bias to avoid
// acne, and both erase the shadow in the last few centimetres before an object touches a surface.
// That is why feet look like they hover. This fills only that band - it is a supplement to the
// shadow map, not a replacement, and can only see what is on screen and in the depth buffer.

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
    float2 TexCoord : TEXCOORD0;
};

// G-Buffer normal target: rgb = worldNormal * 0.5 + 0.5, a = linear RADIAL depth (length(viewPos)).
Texture2D NormalTexture : register(t1);

// Stencil plane of the G-Buffer depth buffer. Non-zero = geometry that does NOT cast shadows (see
// kStencilNonShadowCaster). Integer format: Load only, never Sample - there is no filtering.
Texture2D<uint2> StencilTexture : register(t2);

// Genuine point sampler. A bilinear depth tap interpolates ACROSS silhouettes and returns a depth
// belonging to no real surface - a phantom occluder on exactly the edges this effect lives on.
SamplerState PointSampler : register(s0);

cbuffer ContactShadowBuffer : register(b2)
{
    // World-space direction TOWARD the sun, normalised.
    float3 SunDirection;
    float ContactShadowRayLength;

    float2 ScreenSize;
    float2 InvScreenSize;

    float ContactShadowThickness;
    float ContactShadowIntensity;
    float ContactShadowNormalBias;
    float ContactShadowFadeStart;

    float ContactShadowFadeEnd;
    uint ContactShadowSteps;
    float2 ContactShadowPadding;
};

cbuffer ViewMatrices : register(b12)
{
    column_major matrix matView;
    column_major matrix matProj;
    column_major matrix matInvView;
    column_major matrix InverseProjection;
};

// Interleaved gradient noise, keyed on pixel position ONLY - deliberately not on a frame index.
// There is no TAA in this engine, so a frame-varying pattern would shimmer with nothing to resolve
// it. The fixed pattern is instead resolved spatially by PS_ContactShadowBlur.
float InterleavedGradientNoise(float2 pixelCoord)
{
    return frac(52.9829189f * frac(dot(pixelCoord, float2(0.06711056f, 0.00583715f))));
}

// Reconstructs a view-space position from a screen UV and the RADIAL depth in the normal target's
// alpha. Mirrors PS_DeferredLighting.hlsl so the two cannot disagree.
float3 ReconstructViewPos(float2 uv, float radialDepth)
{
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 viewH = mul(float4(ndc, 1.0f, 1.0f), InverseProjection);
    float3 viewRay = normalize(viewH.xyz / viewH.w);
    return viewRay * radialDepth;
}

float4 main(PS_INPUT input) : SV_TARGET
{
    // Uniform (cbuffer) value, so this is a scalar branch: coherent across the whole draw and it
    // skips every texture fetch below. ContactShadowSettings folds `enabled` into the step count
    // precisely so there is exactly one thing to test.
    if (ContactShadowSteps == 0u)
    {
        return 1.0f;
    }

    float4 normalData = NormalTexture.SampleLevel(PointSampler, input.TexCoord, 0);
    float depth = normalData.a;

    // Sky: the normal target is cleared to zero and never written there.
    if (depth <= 0.0f)
    {
        return 1.0f;
    }

    float3 normal = normalData.rgb * 2.0f - 1.0f;

    // Surfaces facing away from the sun are already black - the lighting pass multiplies by
    // NdotL = max(dot(normal, lightDir), 0) - so marching there could only darken zero, and the ray
    // would immediately self-intersect the surface it started on. Correctness and a large win: this
    // removes roughly half of all lit geometry before a single tap.
    if (dot(normal, SunDirection) <= 0.0f)
    {
        return 1.0f;
    }

    // Distance fade. A correctness requirement, not a quality knob: GBuffer_Normal is RGBA16F, so
    // the radial depth in its alpha is a half float whose ULP grows with distance (~16mm at 35m,
    // ~63mm at 100m). That is the same order as the thickness window tested below, so past the fade
    // the term would be reading quantization noise rather than geometry. It costs nothing visually:
    // a 0.3m ray subtends well under a pixel out there.
    float distFade = 1.0f - saturate((depth - ContactShadowFadeStart) /
                                     max(ContactShadowFadeEnd - ContactShadowFadeStart, 0.001f));
    if (distFade <= 0.0f)
    {
        return 1.0f;
    }

    // Everything below is in view space. This engine is row-vector: the vector goes FIRST.
    // mul(matView, v) would silently apply the inverse rotation and still return a unit-length
    // vector, so the bug reads as "the shadows point the wrong way" rather than as a crash.
    float3 viewPos = ReconstructViewPos(input.TexCoord, depth);
    float3 viewL = normalize(mul(SunDirection, (float3x3) matView));
    float3 viewN = normalize(mul(normal, (float3x3) matView));

    // Push the ray origin off the surface so it cannot shadow itself: a constant plus a term
    // proportional to depth, sized to the fp16 quantization of the depth we compare against. The
    // ray is also implicitly offset along its own direction, since step 0 sits at t >= 0.5/steps.
    float3 rayOrigin = viewPos + viewN * (ContactShadowNormalBias + depth * 0.001f);

    float noise = InterleavedGradientNoise(input.Position.xy);
    float hit = 0.0f;
    float edgeFade = 1.0f;

    [loop]
    for (uint s = 0u; s < ContactShadowSteps; ++s)
    {
        // Jittered, linearly increasing march distance. The +0.5 keeps step 0 off the shading point.
        float t = (float(s) + 0.5f + noise) / float(ContactShadowSteps);
        float3 samplePos = rayOrigin + viewL * (t * ContactShadowRayLength);

        // Project properly through the projection matrix, per step. This deliberately does NOT reuse
        // PS_Ssao's screen-radius approximation: that exists so SSAO can walk a straight line in UV
        // space, and it drags in per-axis P00/P11 scaling and a uniform-clamp trap with it. A sun
        // ray is not axis-aligned in UV space, so projecting for real is both correct and simpler,
        // and makes that whole class of bug unreachable.
        float4 clip = mul(float4(samplePos, 1.0f), matProj);
        if (clip.w <= 0.0001f)
        {
            break;  // Behind the eye.
        }

        float2 sndc = clip.xy / clip.w;
        float2 uv = float2(sndc.x * 0.5f + 0.5f, 0.5f - sndc.y * 0.5f);

        if (any(uv < 0.0f) || any(uv > 1.0f))
        {
            break;  // Off screen: no depth information out there to test against.
        }

        float sampleDepth = NormalTexture.SampleLevel(PointSampler, uv, 0).a;
        if (sampleDepth <= 0.0f)
        {
            continue;  // Sky along the ray: nothing to occlude us, keep marching.
        }

        // The whole test runs in RADIAL depth, and that is precisely why it is correct. `uv` is the
        // projection of samplePos, so the camera ray through `uv` passes through samplePos by
        // construction: length(samplePos) and the stored sampleDepth are both radial distances along
        // the SAME ray, and their difference is a true depth difference. This sidesteps the
        // radial-vs-view-Z trap PS_Ssao.hlsl documents - that pass needs view Z only because it
        // approximates the projection with a screen radius.
        float diff = length(samplePos) - sampleDepth;   // > 0: ray is behind the stored surface.

        // Epsilon sized to the fp16 ULP of GBuffer_Normal.a, with ~2x margin.
        float depthEpsilon = depth * 0.002f;

        // The upper bound is load-bearing. The depth buffer is a heightfield: only the front surface
        // is known, so "the ray is behind this surface" does not mean "the ray is inside this
        // object". Without the bound, a ray passing behind a DISTANT background object reports a hit
        // and shadows a foreground surface that object cannot possibly occlude.
        if (diff > depthEpsilon && diff < ContactShadowThickness + depthEpsilon)
        {
            // Does this occluder cast shadows at all? Grass deliberately casts none (its foliage
            // layer sets castShadows=false, which keeps it out of the shadow-map caster list), so
            // letting it occlude a screen-space ray would reintroduce grass shadows through the back
            // door - and thin alpha-tested blades produce a high-frequency depth mess that reads as
            // noise rather than shadow. The tag is written to stencil during the G-Buffer pass.
            //
            // Deliberately tested only on a hit rather than on every step: hits are rare, so this
            // keeps the extra fetch off the hot path.
            uint2 pixel = uint2(uv * ScreenSize);
            if (StencilTexture.Load(int3(pixel, 0)).g != 0u)
            {
                continue;   // Non-caster: see through it and keep marching.
            }

            hit = 1.0f;

            // Screen-edge fade, evaluated at the hit. A ray whose occluder sits just off-screen
            // finds nothing at all, so without this contact shadows pop in and out as the camera
            // turns. Fading the outer 15% trades a wrong-but-smooth result for a wrong-and-
            // discontinuous one.
            float2 edge = abs(uv * 2.0f - 1.0f);
            edgeFade = 1.0f - saturate((max(edge.x, edge.y) - 0.85f) / 0.15f);
            break;
        }
    }

    return 1.0f - hit * ContactShadowIntensity * distFade * edgeFade;
}
