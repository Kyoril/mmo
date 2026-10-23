// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Ocean surface pixel shader. Writes linear HDR over the lit, fogged opaque scene.
//
// Light paths, summed:
//   * transmitted: the refracted scene behind the surface, attenuated by Beer-Lambert absorption
//     along the view ray through the water,
//   * in-scattered: sun and sky light scattered back out of the water body. Its albedo moves from
//     the trough colour to the crest colour with wave height, and a forward-scattering
//     (Henyey-Greenstein) term lights thin, steep crests from behind when looking toward the sun,
//   * reflected: screen-space reflections, falling back to the sky gradient, weighted by a
//     split-sum environment BRDF whose roughness grows with the slope variance a pixel covers,
//   * the sun highlight: GGX with the same roughness, so distant water glitters but does not alias,
//   * foam: from folding crests (the simulation's Jacobian) and along the shore, on top.
// Height and distance fog finish it exactly like the deferred atmosphere composite.

#include "OceanRenderCommon.hlsli"

Texture2D SceneColorTex : register(t30);   // kSceneColorTextureSlot
Texture2D SceneDepthTex : register(t31);   // kSceneDepthTextureSlot

struct PixelIn
{
    float4 pos : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float2 baseXZ : TEXCOORD1;
    float waveHeight : TEXCOORD2;
    float waterDepth : TEXCOORD3;
    float mask : TEXCOORD4;
};

static const float NO_SCENE_DEPTH = 1.0e6f;

// How much each cascade's foam counts (largest first).
static const float FOAM_CASCADE_WEIGHT[OCEAN_CASCADES] = { 1.0f, 1.0f, 0.35f };

float LoadSceneDistance(int2 pixel)
{
    // Radial distance from the camera; 0 where the opaque pass drew nothing (sky).
    float d = SceneDepthTex.Load(int3(pixel, 0)).a;
    return d > 0.0f ? d : NO_SCENE_DEPTH;
}

float3 LoadSceneColor(float2 uv)
{
    float3 color = SceneColorTex.SampleLevel(OceanClampSampler, uv, 0.0f).rgb;
    // The captured scene can hold NaN/Inf from lighting; never let them into the water.
    return any((asuint(color) & 0x7fffffffu) >= 0x7f800000u) ? float3(0.0f, 0.0f, 0.0f) : min(color, 1e4f);
}

float3 SkyRadiance(float3 direction)
{
    float up = saturate(direction.y);
    return lerp(SkyHorizonLinear, SkyZenithLinear, pow(up, 0.45f));
}

// Karis' analytic fit of the split-sum environment BRDF.
float EnvironmentBrdf(float f0, float roughness, float nDotV)
{
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
    const float4 c1 = float4(1.0f, 0.0425f, 1.04f, -0.04f);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28f * nDotV)) * r.x + r.y;
    float2 ab = float2(-1.04f, 1.04f) * a004 + r.zw;
    return f0 * ab.x + ab.y;
}

float HenyeyGreenstein(float cosTheta, float g)
{
    float g2 = g * g;
    return (1.0f - g2) / (4.0f * OCEAN_PI * pow(max(1.0f + g2 - 2.0f * g * cosTheta, 1e-4f), 1.5f));
}

float SunSpecular(float3 n, float3 v, float3 l, float roughness)
{
    float3 h = normalize(v + l);
    float nDotH = saturate(dot(n, h));
    float nDotL = saturate(dot(n, l));
    float nDotV = max(dot(n, v), 1e-3f);
    float alpha = max(roughness * roughness, 0.002f);
    float a2 = alpha * alpha;
    float denom = nDotH * nDotH * (a2 - 1.0f) + 1.0f;
    float d = a2 / (OCEAN_PI * denom * denom);
    float k = alpha * 0.5f;
    float g = (nDotL / (nDotL * (1.0f - k) + k)) * (nDotV / (nDotV * (1.0f - k) + k));
    float fresnel = 0.02f + 0.98f * pow(1.0f - saturate(dot(v, h)), 5.0f);
    return d * g * fresnel / (4.0f * nDotV) ;
}

// Foam breakup tiles every FOAM_PATTERN_PERIOD metres. The world position is wrapped into that
// range before it is scaled, so the lattice coordinates stay small and the hash keeps full float
// precision anywhere on the map (hashing raw world coordinates degenerated into parallel stripes
// a few kilometres from the origin). Every octave's lattice repeats exactly over the period, so the
// wrap leaves no seam.
static const float FOAM_PATTERN_PERIOD = 256.0f;

float LatticeHash(int2 cell, uint period)
{
    // Cells are never negative here (the domain is wrapped to [0, period) first).
    uint2 q = (uint2)cell % period;
    uint h = (q.x * 1597334677u) ^ (q.y * 3812015801u);
    h = (h ^ (h >> 15)) * 2246822519u;
    h ^= h >> 13;
    return (float)(h & 0xFFFFFFu) / 16777216.0f;
}

// Value noise on a lattice that repeats every `period` cells.
float PeriodicNoise(float2 p, uint period)
{
    int2 i = (int2)floor(p);
    float2 f = p - (float2)i;
    float2 u = f * f * (3.0f - 2.0f * f);
    float a = LatticeHash(i, period);
    float b = LatticeHash(i + int2(1, 0), period);
    float c = LatticeHash(i + int2(0, 1), period);
    float d = LatticeHash(i + int2(1, 1), period);
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

// Bubbly foam breakup: three octaves of value noise. It needs no drift of its own: callers sample
// it at the displaced position, so the pattern rides and churns with the waves.
float FoamPattern(float2 xz)
{
    float2 local = xz - floor(xz / FOAM_PATTERN_PERIOD) * FOAM_PATTERN_PERIOD;
    // Cells per metre 1.25, 3.75 and 9: whole numbers of cells per period, so the wrap is seamless.
    float n = PeriodicNoise(local * 1.25f, 320) * 0.45f;
    n += PeriodicNoise(local * 3.75f + 17.0f, 960) * 0.35f;
    n += PeriodicNoise(local * 9.0f + 41.0f, 2304) * 0.2f;
    return n;
}

bool ProjectToScreen(float3 worldPos, float2 targetSize, out float2 uv, out float radialDistance)
{
    uv = float2(0.0f, 0.0f);
    radialDistance = 0.0f;
    float4 viewPos = mul(float4(worldPos, 1.0f), matView);
    float4 clipPos = mul(viewPos, matProj);
    if (clipPos.w <= 1e-4f)
    {
        return false;
    }

    float2 ndc = clipPos.xy / clipPos.w;
    if (abs(ndc.x) > 1.0f || abs(ndc.y) > 1.0f)
    {
        return false;
    }

    uv = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
    radialDistance = length(viewPos.xyz);
    return true;
}

// Marches the reflected ray through the opaque scene's radial depth. rgb = colour, a = confidence.
float4 TraceReflection(float3 origin, float3 direction, float2 targetSize, float jitter)
{
    const int steps = 24;
    const float maxDistance = 160.0f;
    float previousT = 0.0f;
    float t = 0.5f;

    [loop]
    for (int i = 0; i < steps; ++i)
    {
        // Steps grow geometrically: fine near the surface, long reaches for distant cliffs.
        float nextT = 0.5f + maxDistance * pow(((float)i + jitter) / (float)steps, 2.0f);
        t = max(nextT, previousT + 0.05f);

        float2 uv;
        float rayDistance;
        if (!ProjectToScreen(origin + direction * t, targetSize, uv, rayDistance))
        {
            return float4(0.0f, 0.0f, 0.0f, 0.0f);
        }

        float sceneDistance = LoadSceneDistance(int2(uv * targetSize));
        float behind = rayDistance - sceneDistance;
        if (behind > 0.0f && behind < max((t - previousT) * 2.0f, 1.0f))
        {
            float2 edge = saturate((1.0f - abs(uv * 2.0f - 1.0f)) * 6.0f);
            float confidence = edge.x * edge.y * (1.0f - saturate(t / maxDistance));
            return float4(LoadSceneColor(uv), confidence);
        }

        previousT = t;
    }

    return float4(0.0f, 0.0f, 0.0f, 0.0f);
}

float3 ApplyAtmosphere(float3 color, float3 worldPos)
{
    float3 toPixel = worldPos - CameraPosition;
    float dist = length(toPixel);
    float3 dir = toPixel / max(dist, 1e-4f);

    float transmittance = exp(-FogOpticalDepth(CameraPosition.y, dir.y, dist));
    float3 fogged = color * transmittance + FogSource(dot(dir, SunDirection), 1.0f) * (1.0f - transmittance);
    return ApplyDistanceFog(fogged, DistanceFogAmount(dist));
}

float4 main(PixelIn input) : SV_Target
{
    if (input.mask < 0.5f)
    {
        discard;
    }

    uint targetWidth;
    uint targetHeight;
    SceneColorTex.GetDimensions(targetWidth, targetHeight);
    float2 targetSize = float2((float)targetWidth, (float)targetHeight);
    int2 pixel = int2(input.pos.xy);
    float2 screenUv = input.pos.xy / targetSize;

    float3 toCamera = CameraPosition - input.worldPos;
    float viewDistance = length(toCamera);
    float3 v = toCamera / max(viewDistance, 1e-4f);
    bool fromBelow = CameraPosition.y < input.worldPos.y;

    // --- Surface normal from the cascades' slopes ---------------------------------------------
    float2 slope = float2(0.0f, 0.0f);
    float variance = 0.0f;
    float foamAmount = 0.0f;
    [unroll]
    for (uint cascade = 0; cascade < OCEAN_CASCADES; ++cascade)
    {
        float weight = ShallowWaveWeight(cascade, input.waterDepth);
        float4 surface = OceanSurface.Sample(OceanWrapSampler, float3(input.baseXZ / CascadeSize[cascade], (float)cascade));
        slope += surface.xy * weight;
        // Mip filtering averaged the slopes away; what they averaged over comes back as roughness.
        variance += max(surface.z - dot(surface.xy, surface.xy), 0.0f) * weight * weight;
        // Whitecaps belong to the swell and the chop; the finest ripples barely break.
        foamAmount += surface.w * weight * FOAM_CASCADE_WEIGHT[cascade];
    }

    float3 n = normalize(float3(-slope.x, 1.0f, -slope.y));
    float roughness = saturate(Roughness + pow(0.5f * variance, 0.25f) * 0.6f);

    float3 l = normalize(SunDirection);
    float sunAbove = smoothstep(-0.08f, 0.12f, l.y);
    float3 sunRadiance = SunColor * SunIntensity;

    // --- Refraction ----------------------------------------------------------------------------
    float sceneDistance = LoadSceneDistance(pixel);
    float thickness = max(sceneDistance - viewDistance, 0.0f);

    float2 refractOffset = slope * RefractionStrength * min(thickness, 6.0f) / max(viewDistance * 0.08f, 1.0f);
    float2 refractUv = clamp(screenUv + refractOffset, 0.0f, 1.0f);
    float refractDistance = LoadSceneDistance(int2(refractUv * targetSize));
    if (refractDistance < viewDistance)
    {
        // The offset landed on something in front of the water: use the straight-through pixel.
        refractUv = screenUv;
        refractDistance = sceneDistance;
    }
    float pathLength = max(refractDistance - viewDistance, 0.0f);
    float3 refracted = LoadSceneColor(refractUv);
    float3 transmittance = exp(-Absorption * min(pathLength, 1000.0f));

    if (fromBelow)
    {
        // Looking up at the surface from inside the water: the world above through the moving
        // surface, darkening toward the edge of the Snell window. The underwater post-process adds
        // the water fog.
        float window = smoothstep(0.25f, 0.6f, -v.y);
        float3 deep = TroughColor * (CameraAmbientColor + sunRadiance * sunAbove * 0.25f);
        float3 below = lerp(deep, refracted, window);
        return float4(below, 1.0f);
    }

    // --- In-scattering (the body colour of the water) ----------------------------------------------
    float nDotV = saturate(dot(n, v));
    float crest = saturate(input.waveHeight * CrestHeightScale + 0.5f);
    float3 albedo = lerp(TroughColor, CrestColor, crest);

    float steepness = saturate(length(slope) * 1.6f);
    float phase = HenyeyGreenstein(dot(-l, v), SubsurfaceAnisotropy);
    float sunTransmission = 0.98f * (1.0f - pow(1.0f - saturate(l.y), 5.0f));
    float3 sunIn = sunRadiance * sunTransmission * sunAbove;

    float3 subsurface = sunIn * phase * SubsurfaceStrength * (0.25f + steepness) * (0.5f + crest) * (0.6f + 0.4f * nDotV * nDotV);
    float3 isotropic = (sunIn * (1.0f / (4.0f * OCEAN_PI)) + CameraAmbientColor) * AmbientScatter;
    float3 inscatter = albedo * (1.0f - transmittance) * (subsurface + isotropic) * 4.0f;

    float3 transmitted = refracted * transmittance + inscatter;

    // --- Reflection ----------------------------------------------------------------------------
    float3 r = reflect(-v, n);
    r.y = abs(r.y);
    r = normalize(r);

    // Reflect the sky that is actually on screen (clouds, sun haze, the fogged horizon) where the
    // reflected ray leaves the screen through a sky pixel; the gradient only fills in elsewhere.
    float3 reflection = SkyRadiance(r);
    {
        float2 skyUv;
        float skyDistance;
        if (ProjectToScreen(CameraPosition + r * 20000.0f, targetSize, skyUv, skyDistance)
            && LoadSceneDistance(int2(skyUv * targetSize)) >= NO_SCENE_DEPTH)
        {
            // A rough surface reflects a blurred sky: five taps spread by the roughness.
            float2 spread = roughness * float2(0.06f, 0.035f);
            float3 sky = LoadSceneColor(skyUv) * 0.4f;
            sky += LoadSceneColor(skyUv + float2(spread.x, 0.0f)) * 0.15f;
            sky += LoadSceneColor(skyUv - float2(spread.x, 0.0f)) * 0.15f;
            sky += LoadSceneColor(skyUv + float2(0.0f, spread.y)) * 0.15f;
            sky += LoadSceneColor(skyUv - float2(0.0f, spread.y)) * 0.15f;
            float2 edge = saturate((1.0f - abs(skyUv * 2.0f - 1.0f)) * 8.0f);
            reflection = lerp(reflection, sky, edge.x * edge.y);
        }
    }
    if (viewDistance < 600.0f && ReflectionStrength > 0.0f)
    {
        float jitter = InterleavedGradientNoise(input.pos.xy);
        float4 traced = TraceReflection(input.worldPos + n * 0.15f, r, targetSize, jitter);
        reflection = lerp(reflection, traced.rgb, traced.a * (1.0f - saturate((viewDistance - 400.0f) / 200.0f)));
    }

    float specularWeight = EnvironmentBrdf(0.02f, roughness, max(nDotV, 1e-3f));
    float3 reflected = reflection * specularWeight * ReflectionStrength;

    float3 sunSpecular = sunRadiance * sunAbove * SunSpecular(n, v, l, roughness) * saturate(dot(n, l)) * SunSpecularStrength;

    float3 color = transmitted * (1.0f - specularWeight) + reflected + sunSpecular;

    // --- Foam ----------------------------------------------------------------------------------
    // True water depth under this pixel: the seabed is where the view ray meets the opaque scene.
    // Per pixel, unlike the ocean mask's one value per 4 m quad.
    float seabedDepth = sceneDistance < NO_SCENE_DEPTH
        ? max(input.worldPos.y - (CameraPosition.y - v.y * sceneDistance), 0.0f)
        : 1000.0f;

    // Shore waves: foam bands parallel to the coast, rolling in toward the beach once per
    // SHORE_WAVE_PERIOD and thickening as they arrive. They follow the distance from the shore
    // (from the mask), not the depth: on a steep beach depth-driven foam collapses into a line.
    // The exact waterline gets a lapping fringe from the per-pixel depth.
    float shoreDistance = SampleOceanMask(input.baseXZ).z;
    float shoreZone = 1.0f - saturate(shoreDistance / max(ShoreFoamWidth, 0.01f));
    float shorePhase = shoreDistance / SHORE_WAVE_SPACING + OceanTime / SHORE_WAVE_PERIOD;
    float shoreLines = smoothstep(0.35f, 0.9f, 0.5f + 0.5f * sin(2.0f * OCEAN_PI * shorePhase));
    float fringe = smoothstep(0.35f, 0.04f, seabedDepth);
    float shore = ShoreFoamStrength * (shoreLines * shoreZone * shoreZone * 1.3f + fringe * 1.2f);

    // Foam breaks up into lacy patches: the denser the coverage, the more of the noise pattern
    // turns white, with a soft edge so patches never read as flat decals.
    float pattern = FoamPattern(input.worldPos.xz);
    float coverage = saturate(foamAmount * 1.2f + shore);
    float foam = smoothstep(1.0f - coverage, 1.35f - coverage, pattern) * saturate(coverage * 3.0f);
    foam *= 0.55f + 0.45f * coverage;

    // Foam is a rough, bright scatterer lying on the surface: light it like the beach beside it
    // (albedo / pi, mostly by the sun's elevation), not by the rippled water normal, or it turns
    // grey next to the sand.
    float foamNDotL = saturate(l.y) * 0.75f + saturate(dot(n, l)) * 0.25f;
    float3 foamColor = FoamBrightness * (sunRadiance * sunAbove * foamNDotL * (1.0f / OCEAN_PI) + CameraAmbientColor);
    color = lerp(color, foamColor, foam);

    // Fade into the (already fogged) scene over the last few centimetres of water depth, so the
    // waterline is soft rather than a hard intersection line.
    float3 straightScene = LoadSceneColor(screenUv);
    return float4(lerp(straightScene, ApplyAtmosphere(color, input.worldPos), saturate(seabedDepth / 0.03f)), 1.0f);
}
