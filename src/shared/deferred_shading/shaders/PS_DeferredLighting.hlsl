// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

static const float PI = 3.14159265359;
static const uint NUM_SHADOW_CASCADES = 4;

// Input structure from the vertex shader
struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
    float2 TexCoord : TEXCOORD0;
};

// G-Buffer textures
Texture2D AlbedoTexture : register(t0);
Texture2D NormalTexture : register(t1);
Texture2D MaterialTexture : register(t2);
Texture2D EmissiveTexture : register(t3);
// Screen-space ambient occlusion term (single channel, R). A 1x1 white texture is bound here
// when SSAO is disabled, so this can be sampled unconditionally.
Texture2D SsaoTexture : register(t4);

// Cascade shadow maps
Texture2D ShadowMapCascade0 : register(t5);
Texture2D ShadowMapCascade1 : register(t6);
Texture2D ShadowMapCascade2 : register(t7);
Texture2D ShadowMapCascade3 : register(t8);

// Samplers
// WARNING: despite the name, s0 is NOT a point sampler. RenderLightingPass binds a Trilinear
// filter here, which the half-resolution SSAO upsample depends on. Anything that needs real point
// taps - notably the contact shadow depth march - must use ContactShadowSampler below.
SamplerState PointSampler : register(s0);
SamplerComparisonState ShadowSampler : register(s1);
// Genuine point sampler, for reading depth out of the G-Buffer at arbitrary UVs. Bilinear depth
// taps interpolate across silhouettes and invent surfaces that are not there.
SamplerState ContactShadowSampler : register(s2);

// Poisson disk samples for shadow sampling
static const float2 POISSON_DISK[16] = {
    float2(-0.94201624, -0.39906216),
    float2(0.94558609, -0.76890725),
    float2(-0.094184101, -0.92938870),
    float2(0.34495938, 0.29387760),
    float2(-0.91588581, 0.45771432),
    float2(-0.81544232, -0.87912464),
    float2(-0.38277543, 0.27676845),
    float2(0.97484398, 0.75648379),
    float2(0.44323325, -0.97511554),
    float2(0.53742981, -0.47373420),
    float2(-0.26496911, -0.41893023),
    float2(0.79197514, 0.19090188),
    float2(-0.24188840, 0.99706507),
    float2(-0.81409955, 0.91437590),
    float2(0.19984126, 0.78641367),
    float2(0.14383161, -0.14100790)
};


// Per-view matrices (b12). Must match the layout uploaded by GraphicsDeviceD3D11 (see
// kPerViewMatrixBufferSlot); the per-object world matrix at b0 is not needed in this pass.
cbuffer ViewMatrices : register(b12)
{
    column_major matrix matView;
    column_major matrix matProj;
    column_major matrix matInvView;
    column_major matrix InverseProjection;
};

// Camera constant buffer
cbuffer CameraBuffer : register(b1)
{
    float3 CameraPosition;
    float FogStart;
    float FogEnd;
    float3 FogColor;
    column_major matrix InverseViewMatrix;
    float Time;
    float3 _CameraPadding;
}

// Light structure (matches StructuredBuffer element in C++)
struct Light
{
    float3 Position;
    float Range;
    float3 Color;
    float Intensity;
    float3 Direction;
    float SpotAngle;
    uint Type;  // 0 = Point, 1 = Directional, 2 = Spot
    uint ShadowMap;
    float2 Padding;
};

// Light metadata constant buffer (small - just count and ambient color)
cbuffer LightMetadata : register(b2)
{
    uint LightCount;
    float3 AmbientColor;
}

// Structured buffer containing light data (can hold many more lights than constant buffers)
StructuredBuffer<Light> Lights : register(t9);

cbuffer ShadowBuffer : register(b3)
{
    // Cascade view-projection matrices
    column_major matrix CascadeViewProj[NUM_SHADOW_CASCADES];
    
    // Cascade split distances (in view space)
    float4 CascadeSplitDistances;
    
    // Shadow filter parameters
    float ShadowBias;           // Depth bias to prevent shadow acne
    float NormalBiasScale;      // Bias scale factor based on normal
    float ShadowSoftness;       // Controls general softness of shadows
    float BlockerSearchRadius;  // Search radius for the blocker search phase
    
    float LightSize;            // Controls the size of the virtual light (larger = softer shadows)
    uint CascadeCount;          // Number of active cascades
    uint DebugCascades;         // Whether to show cascade debug colors
    float CascadeBlendFactor;   // Blend factor for cascade transitions

    uint PcfSampleCount;        // Number of PCF taps per shadow lookup (shadow quality)
    uint SsaoDebugMode;         // Non-zero: lighting pass outputs the raw SSAO term instead of the lit scene.

    // Contact shadow parameters. MUST stay field-for-field in sync with struct ShadowBuffer in
    // deferred_renderer.cpp - a drift of one field silently makes every field after it read
    // garbage, with no error anywhere. All distances are world units (1 unit = 1 metre).
    uint ContactShadowSteps;        // 0 disables the march entirely
    uint ContactShadowDebugMode;    // Non-zero: output the raw contact shadow term
    float ContactShadowRayLength;
    float ContactShadowThickness;
    float ContactShadowIntensity;
    float ContactShadowNormalBias;
    float ContactShadowFadeStart;
    float ContactShadowFadeEnd;

    float2 _ShadowPadding;
};

// Debug colors for cascade visualization
static const float3 CASCADE_COLORS[4] = {
    float3(1.0, 0.0, 0.0),  // Red - cascade 0 (nearest)
    float3(0.0, 1.0, 0.0),  // Green - cascade 1
    float3(0.0, 0.0, 1.0),  // Blue - cascade 2
    float3(1.0, 1.0, 0.0)   // Yellow - cascade 3 (farthest)
};

// Reconstructs world position from depth
float3 ReconstructPosition(float linearDepth, float3 viewRay)
{
    float3 viewPos = viewRay * linearDepth;
    return mul(float4(viewPos, 1.0), matInvView).xyz;
}

float3 F_Schlick(float3 F0, float cosTheta)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float D_GGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}


float G_SchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float G_Smith(float NdotV, float NdotL, float roughness)
{
    return G_SchlickGGX(NdotV, roughness) * G_SchlickGGX(NdotL, roughness);
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


// Calculates point light contribution
float3 CalculatePointLight(Light light, float3 viewDir, float3 worldPos, float3 normal, float3 albedo, float metallic, float roughness, float specular)
{
    float3 lightDir = light.Position - worldPos;
    float distance = length(lightDir);

    if (distance > light.Range)
        return float3(0, 0, 0);
    
    lightDir = normalize(lightDir);
    float3 halfway = normalize(lightDir + viewDir);

    float NdotL = max(dot(normal, lightDir), 0.0);
    float NdotV = max(dot(normal, viewDir), 0.0);
    float NdotH = max(dot(normal, halfway), 0.0);
    float VdotH = max(dot(viewDir, halfway), 0.0);
    
    // Attenuation
    float attenuation = pow(1.0 - saturate(distance / light.Range), 2.0);
    
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float3 F = F_Schlick(F0, VdotH);
    float D = D_GGX(NdotH, roughness);
    float G = G_Smith(NdotV, NdotL, roughness);

    float3 specularBRDF = (D * G * F) / (4.0 * max(NdotV * NdotL, 0.001)) * saturate(specular);
    float3 kS = F;
    float3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;

    float3 diffuse = albedo / PI;

    float3 radiance = light.Color * light.Intensity * attenuation;

    return (kD * diffuse + specularBRDF) * radiance * NdotL;
}

// Helper function to find average blocker depth for a specific cascade
float FindBlockerDepthCascade(Texture2D shadowMap, float2 uv, float receiverDepth, float searchRadius, out float numBlockers)
{
    float blockerSum = 0.0f;
    numBlockers = 0.0f;
    
    // Generate a random rotation angle to reduce banding artifacts
    float angle = frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453) * PI;
    float cosAngle = cos(angle);
    float sinAngle = sin(angle);
    float2x2 rot = float2x2(cosAngle, -sinAngle, sinAngle, cosAngle);
    
    // Sample potential blockers
    [unroll]
    for (int i = 0; i < 16; i++)
    {
        float2 offset = mul(rot, POISSON_DISK[i]) * searchRadius;
        float shadowMapDepth = shadowMap.SampleLevel(PointSampler, uv + offset, 0).r;
        
        // If the depth is less than the receiver's depth, it's a blocker
        if (shadowMapDepth < receiverDepth - ShadowBias)
        {
            blockerSum += shadowMapDepth;
            numBlockers++;
        }
    }
    
    if (numBlockers > 0.0f)
    {
        return blockerSum / numBlockers;
    }
        
    return 1.0f;
}

// Optimized PCF filter with rotated Poisson disk
float PCF_FilterCascade(Texture2D shadowMap, float2 uv, float receiverDepth, float filterRadius)
{
    float sum = 0.0f;
    
    // Generate a random rotation angle to reduce banding artifacts
    float angle = frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453) * PI;
    float cosAngle = cos(angle);
    float sinAngle = sin(angle);
    float2x2 rot = float2x2(cosAngle, -sinAngle, sinAngle, cosAngle);

    // Sample count is driven by the shadow-quality setting (clamped to the 16-entry Poisson disk).
    // Fewer taps = cheaper but slightly noisier/harder shadow edges.
    uint sampleCount = clamp(PcfSampleCount, 1u, 16u);
    [loop]
    for (uint i = 0; i < sampleCount; i++)
    {
        float2 offset = mul(rot, POISSON_DISK[i]) * filterRadius;
        sum += shadowMap.SampleCmpLevelZero(ShadowSampler, uv + offset, receiverDepth - ShadowBias);
    }

    return sum / (float)sampleCount;
}

// Sample shadow from a specific cascade
float SampleShadowCascade(Texture2D shadowMap, float3 worldPos, float3 normal, uint cascadeIndex)
{
    // Apply normal-based bias to reduce shadow acne on sloped surfaces
    float3 lightDir = -normalize(Lights[0].Direction);
    float NdotL = saturate(dot(normal, lightDir));
    
    // Scale bias based on slope - more bias for grazing angles
    float slopeBias = NormalBiasScale * (1.0 - NdotL);
    float3 biasedWorldPos = worldPos + normal * slopeBias;
    
    // Transform to light space
    float4 lightSpacePos = mul(float4(biasedWorldPos, 1.0f), CascadeViewProj[cascadeIndex]);
    
    if (lightSpacePos.w <= 0.0f)
    {
        return 1.0f;
    }
        
    // Perspective division
    float3 ndc = lightSpacePos.xyz / lightSpacePos.w;
    
    // Convert to texture coordinates
    float2 uv = ndc.xy * float2(1.0f, -1.0f) * 0.5f + 0.5f;
    float receiverDepth = ndc.z;
    
    // Early out if outside shadow map
    if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f || ndc.z > 1.0f || ndc.z < 0.0f)
    {
        return 1.0f;
    }
    
    // Check if surface is facing light
    if (NdotL <= 0.0f)
    {
        return 0.0f;  // Surface faces away from light
    }
    
    // Calculate filter radius based on cascade (larger cascades need larger filter radius in UV space)
    // But since each cascade has similar world-space coverage per texel, use consistent filtering
    float baseFilterRadius = 1.0f / 2048.0f;  // Base texel size
    float filterRadius = baseFilterRadius * (1.0f + float(cascadeIndex) * 0.5f) * ShadowSoftness;
    
    // Use simpler PCF for quality/performance balance
    return PCF_FilterCascade(shadowMap, uv, receiverDepth, filterRadius);
}

// Select cascade and sample shadow with optional blending
float SampleShadowCSM(float3 worldPos, float3 normal, float viewDepth, out uint selectedCascade)
{
    selectedCascade = 0;
    
    // Early out if no cascades
    if (CascadeCount == 0)
    {
        return 1.0f;
    }
    
    // Find the appropriate cascade based on view depth
    // Cascades are ordered by distance, so find the first one that contains our depth
    float cascadeSplits[4] = { 
        CascadeSplitDistances.x, 
        CascadeSplitDistances.y, 
        CascadeSplitDistances.z, 
        CascadeSplitDistances.w 
    };
    
    // Select cascade - find the first cascade where our depth is within range
    selectedCascade = CascadeCount - 1;  // Default to last cascade
    for (uint i = 0; i < CascadeCount; i++)
    {
        if (viewDepth <= cascadeSplits[i])
        {
            selectedCascade = i;
            break;
        }
    }
    
    // Sample the selected cascade
    float shadow = 1.0f;
    
    // Use switch for proper texture sampling (can't index textures dynamically in SM5)
    switch (selectedCascade)
    {
        case 0: shadow = SampleShadowCascade(ShadowMapCascade0, worldPos, normal, 0); break;
        case 1: shadow = SampleShadowCascade(ShadowMapCascade1, worldPos, normal, 1); break;
        case 2: shadow = SampleShadowCascade(ShadowMapCascade2, worldPos, normal, 2); break;
        case 3: shadow = SampleShadowCascade(ShadowMapCascade3, worldPos, normal, 3); break;
    }
    
    // Optional: Blend between cascades for smoother transitions
    if (CascadeBlendFactor > 0.0f && selectedCascade < CascadeCount - 1)
    {
        float currentSplit = cascadeSplits[selectedCascade];
        float blendStart = currentSplit * (1.0f - CascadeBlendFactor);
        
        if (viewDepth > blendStart)
        {
            float blendWeight = (viewDepth - blendStart) / (currentSplit - blendStart);
            blendWeight = smoothstep(0.0f, 1.0f, blendWeight);
            
            float nextShadow = 1.0f;
            switch (selectedCascade + 1)
            {
                case 1: nextShadow = SampleShadowCascade(ShadowMapCascade1, worldPos, normal, 1); break;
                case 2: nextShadow = SampleShadowCascade(ShadowMapCascade2, worldPos, normal, 2); break;
                case 3: nextShadow = SampleShadowCascade(ShadowMapCascade3, worldPos, normal, 3); break;
            }
            
            shadow = lerp(shadow, nextShadow, blendWeight);
        }
    }
    
    return shadow;
}

// Interleaved gradient noise, keyed on pixel position ONLY — deliberately not on a frame index.
// There is no TAA in this engine, so a frame-varying pattern would shimmer with nothing to resolve
// it. Copied from PS_Ssao.hlsl, but note the conclusion does NOT carry over: SSAO resolves its fixed
// pattern spatially with a bilateral blur, and the contact shadow march has no blur pass. See the
// note on jitter in ComputeContactShadow.
float InterleavedGradientNoise(float2 pixelCoord)
{
    return frac(52.9829189f * frac(dot(pixelCoord, float2(0.06711056f, 0.00583715f))));
}

// Screen-space contact shadows: marches a short ray toward the sun through the G-Buffer's depth to
// fill in the small-scale occlusion the cascaded shadow maps are too coarse to resolve — a
// character's feet on the ground, a crate against a wall. Returns a [0,1] multiplier for the CSM
// shadow term.
//
// The CSM's depth bias and finite resolution are what erase the shadow in the last few centimetres
// before contact, which is exactly the band this fills. It is a supplement to the shadow map, not a
// replacement: it can only see what is on screen and in the depth buffer.
//
// `viewPos` is the shading point in view space; `depth` is its RADIAL depth (length(viewPos)) — the
// same quantity GBuffer_Normal.a stores. `viewDepth` is distance from the camera in world space.
float ComputeContactShadow(float3 lightDir, float3 normal, float3 viewPos, float depth, float viewDepth, float2 pixelCoord)
{
    // ContactShadowSteps is a cbuffer value, so this is a scalar branch: uniform across the whole
    // draw, and it skips the entire loop including every texture fetch. This is the "costs nothing
    // when switched off" path, and the reason ContactShadowSettings folds `enabled` into the step
    // count rather than passing a separate flag.
    if (ContactShadowSteps == 0u)
    {
        return 1.0f;
    }

    // Sky. GBuffer_Normal is cleared to zero and never written there, so depth 0 means "no surface".
    if (depth <= 0.0f)
    {
        return 1.0f;
    }

    // Surfaces facing away from the sun are already black — CalculateDirectionalLight multiplies by
    // NdotL = max(dot(normal, lightDir), 0) — so marching there could only darken zero, and the ray
    // would immediately self-intersect the surface it started on. Correctness and a large win: this
    // takes out roughly half of all lit geometry before a single tap.
    if (dot(normal, lightDir) <= 0.0f)
    {
        return 1.0f;
    }

    // Distance fade. This is a correctness requirement, not a quality knob: GBuffer_Normal is
    // RGBA16F, so the radial depth in its alpha is a half float whose ULP grows with distance
    // (~16mm at 35m, ~63mm at 100m). That is the same order as the thickness window tested below,
    // so past the fade the term would be reading quantization noise rather than geometry. It costs
    // nothing visually: a 0.3m ray subtends well under a pixel at that range.
    float distFade = 1.0f - saturate((viewDepth - ContactShadowFadeStart) /
                                     max(ContactShadowFadeEnd - ContactShadowFadeStart, 0.001f));
    if (distFade <= 0.0f)
    {
        return 1.0f;
    }

    // From here on everything is in view space. This engine is row-vector: the vector goes FIRST.
    // mul(matView, v) would silently apply the inverse rotation and still hand back a unit-length
    // vector, so the bug looks like "the shadows point the wrong way" rather than a crash.
    float3 viewL = normalize(mul(lightDir, (float3x3) matView));
    float3 viewN = normalize(mul(normal, (float3x3) matView));

    // Push the ray origin off the surface so it cannot shadow itself. Two terms: a constant, and one
    // proportional to depth, sized to the fp16 quantization of the depth we are about to compare
    // against. The ray is also implicitly offset along its own direction, since the first sample
    // sits at t >= 0.5/steps rather than at 0.
    float3 rayOrigin = viewPos + viewN * (ContactShadowNormalBias + depth * 0.001f);

    float noise = InterleavedGradientNoise(pixelCoord);
    float hit = 0.0f;
    float edgeFade = 1.0f;

    [loop]
    for (uint s = 0u; s < ContactShadowSteps; ++s)
    {
        // Jittered, linearly increasing march distance. The +0.5 keeps step 0 off the shading point
        // itself. Same idiom as the SSAO march.
        //
        // Jitter here trades banding for per-pixel noise rather than removing error: the hit test is
        // binary, there is no blur pass and no TAA to resolve the pattern. That is the real cost of
        // marching inline instead of in a pass of its own, and it is tolerable because the term is
        // confined to a narrow band at contacts rather than washing the whole screen, and is
        // modulated down by intensity and both fades. If it ever reads as too noisy the lever is
        // gxContactShadowQuality (more steps) or a shorter gxContactShadowLength — reach for a blur
        // pass only with evidence that neither is enough.
        float t = (float(s) + 0.5f + noise) / float(ContactShadowSteps);
        float3 samplePos = rayOrigin + viewL * (t * ContactShadowRayLength);

        // Project properly through the projection matrix, per step. This deliberately does NOT reuse
        // the SSAO pass's screen-radius approximation: that exists so SSAO can walk a straight line
        // in UV space, and it drags in per-axis P00/P11 scaling and a uniform-clamp trap along with
        // it. A sun ray is not axis-aligned in UV space, so projecting for real is both correct and
        // simpler, and makes that whole class of bug unreachable.
        float4 clip = mul(float4(samplePos, 1.0f), matProj);
        if (clip.w <= 0.0001f)
        {
            break;  // Behind the eye.
        }

        float2 sndc = clip.xy / clip.w;
        // Inverse of the ndc built in main(): ndc = (u * 2 - 1, 1 - v * 2).
        float2 uv = float2(sndc.x * 0.5f + 0.5f, 0.5f - sndc.y * 0.5f);

        if (any(uv < 0.0f) || any(uv > 1.0f))
        {
            break;  // Off screen: there is no depth information out there to test against.
        }

        // Point sampler (s2), never the Trilinear s0 — see the sampler declarations at the top.
        float sampleDepth = NormalTexture.SampleLevel(ContactShadowSampler, uv, 0).a;
        if (sampleDepth <= 0.0f)
        {
            continue;  // Sky along the ray: nothing there to occlude us, keep marching.
        }

        // The whole test runs in RADIAL depth, and that is precisely why it is correct here. `uv` is
        // the projection of samplePos, so the camera ray through `uv` passes through samplePos by
        // construction: length(samplePos) and the stored sampleDepth are therefore both radial
        // distances measured along the SAME ray, and their difference is a true depth difference.
        // This sidesteps the radial-vs-view-Z trap PS_Ssao.hlsl documents — that pass needs view Z
        // only because it approximates the projection with a screen radius. (The tap lands on a
        // texel centre whose ray differs from samplePos's by a sub-texel angle; that error is orders
        // of magnitude below the epsilon below.)
        float diff = length(samplePos) - sampleDepth;   // > 0: the ray is behind the stored surface.

        // Epsilon sized to the fp16 ULP of GBuffer_Normal.a, with ~2x margin.
        float depthEpsilon = depth * 0.002f;

        // The upper bound is load-bearing, not a tuning nicety. The depth buffer is a heightfield:
        // only the front surface is known, so "the ray is behind this surface" does not mean "the
        // ray is inside this object". Without the bound, a ray passing behind a DISTANT background
        // object reports a hit and shadows a foreground surface that object cannot possibly occlude.
        if (diff > depthEpsilon && diff < ContactShadowThickness + depthEpsilon)
        {
            hit = 1.0f;

            // Screen-edge fade, evaluated at the hit. A ray whose occluder sits just off-screen
            // finds nothing at all, so without this, contact shadows pop in and out as the camera
            // turns. Fading the outer 15% of the frame trades a wrong-but-smooth result for a
            // wrong-and-discontinuous one.
            float2 edge = abs(uv * 2.0f - 1.0f);
            edgeFade = 1.0f - saturate((max(edge.x, edge.y) - 0.85f) / 0.15f);
            break;
        }
    }

    return 1.0f - hit * ContactShadowIntensity * distFade * edgeFade;
}

// Calculates directional light contribution
float3 CalculateDirectionalLight(Light light, float3 viewDir, float3 worldPos, float3 normal, float3 albedo, float metallic, float roughness, float specular, float shadow)
{
    float3 lightDir = -normalize(light.Direction);

    float3 halfway = normalize(lightDir + viewDir);

    float NdotL = max(dot(normal, lightDir), 0.0);
    float NdotV = max(dot(normal, viewDir), 0.0);
    float NdotH = max(dot(normal, halfway), 0.0);
    float VdotH = max(dot(viewDir, halfway), 0.0);

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float3 F = F_Schlick(F0, VdotH);
    float D = D_GGX(NdotH, roughness);
    float G = G_Smith(NdotV, NdotL, roughness);

    float3 specularBRDF = (D * G * F) / (4.0 * max(NdotV * NdotL, 0.001));
    specularBRDF *= specular;

    float3 kS = F;
    float3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;

    float3 diffuse = albedo / PI;

    float3 radiance = light.Color * light.Intensity;

    return ((kD * diffuse + specularBRDF) * radiance * NdotL) * shadow;
}

// Calculates spot light contribution
float3 CalculateSpotLight(Light light, float3 viewDir, float3 worldPos, float3 normal, float3 albedo, float metallic, float roughness, float specular)
{
    float3 lightDir = light.Position - worldPos;
    float distance = length(lightDir);

    if (distance > light.Range)
        return float3(0, 0, 0);

    lightDir = normalize(lightDir);

    // Spot cone
    float spotFactor = dot(lightDir, -normalize(light.Direction));
    float spotCutoff = cos(radians(light.SpotAngle * 0.5));
    if (spotFactor < spotCutoff)
        return float3(0, 0, 0);

    float spotAttenuation = smoothstep(spotCutoff, spotCutoff + 0.1, spotFactor);

    // Distance attenuation
    float attenuation = pow(1.0 - saturate(distance / light.Range), 2.0) * spotAttenuation;

    // Cook-Torrance lighting
    float3 halfway = normalize(lightDir + viewDir);

    float NdotL = max(dot(normal, lightDir), 0.0);
    float NdotV = max(dot(normal, viewDir), 0.0);
    float NdotH = max(dot(normal, halfway), 0.0);
    float VdotH = max(dot(viewDir, halfway), 0.0);

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float3 F = F_Schlick(F0, VdotH);
    float D = D_GGX(NdotH, roughness);
    float G = G_Smith(NdotV, NdotL, roughness);

    float3 specularBRDF = (D * G * F) / (4.0 * max(NdotV * NdotL, 0.001));
    specularBRDF *= specular;

    float3 kS = F;
    float3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;

    float3 diffuse = albedo / PI;
    float3 radiance = light.Color * light.Intensity * attenuation;

    return (kD * diffuse + specularBRDF) * radiance * NdotL;
}

float4 main(PS_INPUT input) : SV_TARGET
{
    // Sample G-Buffer textures
    float4 albedoData = AlbedoTexture.Sample(PointSampler, input.TexCoord);
    float4 normalData = NormalTexture.Sample(PointSampler, input.TexCoord);
    float4 materialData = MaterialTexture.Sample(PointSampler, input.TexCoord);
    float4 emissiveData = EmissiveTexture.Sample(PointSampler, input.TexCoord);
    float depth = normalData.a;

    // Reconstruct the view-space ray direction for this pixel from the inverse projection matrix
    // and the screen UV, replacing the former ViewRay G-Buffer target. The G-Buffer stored
    // viewRay = normalize(viewPos) and depth = length(viewPos); unprojecting the pixel's NDC gives
    // a point on the exact same ray from the camera, so its normalised direction is identical.
    float2 ndc = float2(input.TexCoord.x * 2.0f - 1.0f, 1.0f - input.TexCoord.y * 2.0f);
    float4 viewH = mul(float4(ndc, 1.0f, 1.0f), InverseProjection);
    float3 viewRay = normalize(viewH.xyz / viewH.w);
    
    // Extract G-Buffer data
    float3 albedo = albedoData.rgb;
    float opacity = albedoData.a;
    
    // Transform normal from [0, 1] to [-1, 1]
    float3 normal = normalData.rgb * 2.0f - 1.0f;
    
    float metallic = materialData.r;
    float roughness = materialData.g;
    float specular = materialData.b;
    // Combine the material's baked AO with the screen-space AO term. Sampled with a linear
    // sampler so a half-resolution AO target upsamples smoothly.
    float ssao = SsaoTexture.Sample(PointSampler, input.TexCoord).r;
    float ao = materialData.a * ssao;
    
    float3 emissive = emissiveData.rgb;
    
    // Reconstruct world position from depth
    float3 worldPos = ReconstructPosition(depth, viewRay);

    // Calculate view depth for cascade selection - use distance from camera
    float viewDepth = length(worldPos - CameraPosition);

    // Initialize lighting with ambient and emissive
    float3 lighting = albedo * AmbientColor * ao + emissive;
    
    float3 viewDir = normalize(CameraPosition - worldPos);
    
    // Debug cascade visualization color
    float3 cascadeDebugColor = float3(1.0, 1.0, 1.0);

    // Raw contact shadow term, kept for the debug view. Stays 1 (unoccluded) when the effect is off
    // or the pixel takes one of the march's early-outs.
    float contactShadowDebug = 1.0f;

    // Calculate lighting for each light
    for (uint i = 0; i < LightCount; i++)
    {
        Light light = Lights[i];
        
        if (light.Type == 0) // Point light
        {
            lighting += CalculatePointLight(light, viewDir, worldPos, normal, albedo, metallic, roughness, specular);
        }
        else if (light.Type == 1) // Directional light
        {
            float shadow = 1.0f;
            if (light.ShadowMap > 0 && CascadeCount > 0)
            {
                uint selectedCascade = 0;
                shadow = SampleShadowCSM(worldPos, normal, viewDepth, selectedCascade);
                
                // Store cascade debug color
                if (DebugCascades > 0)
                {
                    cascadeDebugColor = CASCADE_COLORS[selectedCascade];
                }
            }

            // Contact shadows are applied OUTSIDE the block above on purpose. They need no shadow
            // map and no cascades, only the G-Buffer, so a scene with shadow maps off still gets
            // them. They multiply the CSM term rather than replace it: the CSM resolves the
            // large-scale occlusion, this fills in the sub-texel detail it cannot.
            //
            // The guard skips the march wherever the CSM already fully occludes, since the term
            // could only darken black. Unlike the early-outs inside ComputeContactShadow this one is
            // pixel-varying, so it pays off per-wave rather than per-pixel — still worth it, as
            // fully shadowed regions are large and contiguous. Note it never fires when there is no
            // shadow map (shadow stays 1.0), which is exactly the case that must keep working.
            if (shadow > 0.001f)
            {
                float3 sunDir = -normalize(light.Direction);
                contactShadowDebug = ComputeContactShadow(sunDir, normal, viewRay * depth, depth, viewDepth, input.Position.xy);
                shadow *= contactShadowDebug;
            }

            lighting += CalculateDirectionalLight(light, viewDir, worldPos, normal, albedo, metallic, roughness, specular, shadow);
        }
        else if (light.Type == 2) // Spot light
        {
            lighting += CalculateSpotLight(light, viewDir, worldPos, normal, albedo, metallic, roughness, specular);
        }   
    }
    
    // Apply cascade debug visualization
    if (DebugCascades > 0)
    {
        lighting = lerp(lighting, lighting * cascadeDebugColor, 0.3);
    }
    
    // Apply fog
    float distanceToCamera = length(worldPos - CameraPosition);
	float fogFactor = saturate((distanceToCamera - FogStart) / (FogEnd - FogStart));
    lighting = lerp(lighting, FogColor, fogFactor);

    // Apply ACES tone mapping
    lighting = ACESFilm(lighting);
    
    // Apply gamma correction
    lighting = pow(lighting, 1.0 / 2.2);

    // SSAO debug visualization: show the raw AO term instead of the lit scene.
    if (SsaoDebugMode != 0)
    {
        return float4(ssao.xxx, 1.0f);
    }

    // Contact shadow debug visualization: white = unoccluded, black = fully occluded. A healthy
    // image is near-white with thin dark bands at contacts; a grey wash or moire on flat ground is
    // the surface shadowing itself, and wants a higher gxContactShadowBias. If both debug modes are
    // on, SSAO wins by being checked first - they are alternative diagnostics, not a blend.
    if (ContactShadowDebugMode != 0)
    {
        return float4(contactShadowDebug.xxx, 1.0f);
    }

    return float4(lighting, opacity);
}
