// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Fills every froxel with this frame's fog: extinction from height fog times wind-scrolled noise, and
// the light scattered toward the camera. u0: rgb = (fog ambient + unshadowed point and spot lights) *
// sigmaScatter, a = sigmaTotal; jittered and accumulated by CS_FogTemporal. u1: rgb = sigmaScatter * sun
// visibility - the shadowed sun WITHOUT its phase function, which CS_FogIntegrate applies for the current
// view rays. u1 is deterministic per cell and bypasses the history.
// sigmaTotal is the zone fog's sigma plus every local fog volume's density at the froxel, sigmaScatter
// the same sum with each volume's density tinted by its colour. Local volumes share the zone fog's
// lighting.
//
// Point and spot lights come from the deferred renderer's light buffer (t9), local fog volumes from the
// fog volume buffer (t10). Each 8x8x8 thread group culls the frame's lights and volumes once against its
// block's bounding sphere into group-shared lists, so a froxel only evaluates the lights and volumes
// that can reach its block.

#include "VolumetricFogCommon.hlsli"
#include "LightCommon.hlsli"
#include "FogVolumeCommon.hlsli"

static const uint NUM_SHADOW_CASCADES = 4;

Texture3D<float> NoiseVolume : register(t0);
Texture2D ShadowMapCascade0 : register(t5);
Texture2D ShadowMapCascade1 : register(t6);
Texture2D ShadowMapCascade2 : register(t7);
Texture2D ShadowMapCascade3 : register(t8);

SamplerState NoiseSampler : register(s0);
SamplerComparisonState ShadowSampler : register(s1);

// MUST stay field-for-field in sync with struct ShadowBuffer in deferred_renderer.cpp. Only the leading
// fields are read here.
cbuffer ShadowBuffer : register(b3)
{
    column_major matrix CascadeViewProj[NUM_SHADOW_CASCADES];
    float4 CascadeSplitDistances;
    float ShadowBias;
    float NormalBiasScale;
    float ShadowSoftness;
    float BlockerSearchRadius;
    float LightSize;
    uint CascadeCount;
};

RWTexture3D<float4> InjectOutput : register(u0);
RWTexture3D<float4> InjectSunOutput : register(u1);

StructuredBuffer<Light> Lights : register(t9);
StructuredBuffer<FogVolume> FogVolumes : register(t10);
Texture3D<float4> ColumnFarDepth : register(t11);
Texture2D FogShadowMap : register(t12);  // coarse sun shadow over the whole fog range (FogShadowViewProj), when FogShadowValid  // r = farthest depth seen through each grid column (CS_FogDepthTiles)

static const uint FOG_GROUP_SIZE = 8;
static const uint FOG_GROUP_THREADS = FOG_GROUP_SIZE * FOG_GROUP_SIZE * FOG_GROUP_SIZE;
static const uint MAX_BLOCK_LIGHTS = 64; // mirrors light_math::MaxLightsPerFogBlock
static const uint MAX_BLOCK_VOLUMES = 16; // mirrors fog_volume::MaxVolumesPerBlock

groupshared uint s_blockLightHits;                   // lights that reach the block (may exceed the capacity)
groupshared uint s_blockLights[MAX_BLOCK_LIGHTS];    // indices into Lights
groupshared uint s_blockVolumeHits;                  // fog volumes that reach the block (may exceed the capacity)
groupshared uint s_blockVolumes[MAX_BLOCK_VOLUMES];  // indices into FogVolumes

// Zone fog extinction at a world position: height fog times the wind-scrolled noise.
float ZoneSigmaAt(float3 worldPos)
{
    if ((DiagFlags & 8u) != 0)
    {
        return FogDensityAt(worldPos.y);
    }

    // The pattern moves with the wind: the noise at p now is the noise that was at p - offset.
    float3 noiseUvw = float3(worldPos.x / NoiseSize - WindOffset.x, worldPos.y / NoiseSize, worldPos.z / NoiseSize - WindOffset.y);
    return FogDensityAt(worldPos.y) * NoiseDensityFactor(NoiseVolume.SampleLevel(NoiseSampler, noiseUvw, 0.0f), NoiseAmount);
}

// Box-filters the sun's shadowed scattering over a cell with one tap pattern. A tap's world position is
// CameraPosition + depth * (ray + sub.x * cellX + sub.y * cellY) (all per metre of view depth); the cascade
// projection is orthographic, so its shadow-space position is the same affine expression in light space.
// Returns x = mean(visibility * zone sigma), y = mean(visibility), z = mean(zone sigma). shadowed = false treats
// every tap as lit.
float3 SumCellSunTaps(Texture2D shadowMap, bool shadowed, float3 lightOrigin, float3 lightRay, float3 lightX, float3 lightY,
    float3 rayPerDepth, float3 cellX, float3 cellY, uint3 id)
{
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    [loop]
    for (uint k = 0; k < SunTapsZ; ++k)
    {
        // With a history the pattern is jittered per frame (Jitter/ScreenJitter, 0.5 = centred otherwise).
        float depth = SliceToDepth((float(id.z) + (float(k) + Jitter) / float(SunTapsZ)) / float(GridDepth));

        // Each depth layer shifts its xy strata by an R2 sequence step, so the layers do not stack their
        // taps in the same columns and together cover the cell's cross-section evenly.
        float2 layerOffset = frac(ScreenJitter + float(k) * float2(0.7548777f, 0.5698403f));
        [loop]
        for (uint j = 0; j < SunTapsXY; ++j)
        {
            [loop]
            for (uint i = 0; i < SunTapsXY; ++i)
            {
                float2 sub = (float2(i, j) + layerOffset) / float(SunTapsXY) - 0.5f;

                float visibility = 1.0f;
                float3 light = lightOrigin + depth * (lightRay + sub.x * lightX + sub.y * lightY);
                float2 uv = light.xy * float2(0.5f, -0.5f) + 0.5f;
                if (shadowed && (DiagFlags & 4u) == 0 && all(uv >= 0.0f) && all(uv <= 1.0f) && light.z >= 0.0f && light.z <= 1.0f)
                {
                    visibility = shadowMap.SampleCmpLevelZero(ShadowSampler, uv, light.z - ShadowBias);
                }

                // The fog density is filtered with the same taps: a single noise sample per cell slides
                // through noise features smaller than the cell as the camera moves, and the shafts show it.
                float3 worldPos = CameraPosition + depth * (rayPerDepth + sub.x * cellX + sub.y * cellY);
                float zoneSigma = ZoneSigmaAt(worldPos);
                sum += float3(visibility * zoneSigma, visibility, zoneSigma);
            }
        }
    }

    return sum / float(SunTapsXY * SunTapsXY * SunTapsZ);
}

// Direction through a screen UV scaled to one metre of view depth.
float3 RayPerDepth(float2 uv)
{
    float3 ray = FogWorldRay(uv);
    return ray / max(dot(ray, CameraForward), 1e-4f);
}

// The sun's shadowed scattering of froxel id, box-filtered over the whole cell (SunTapsXY^2 x SunTapsZ taps
// on a fixed pattern) with the same taps every frame: x = mean(visibility * zone fog sigma), y = mean
// visibility (for the local fog volumes, which are evaluated at the cell centre only), z = mean zone fog sigma
// (the cell's extinction, so it is as deterministic as the shafts it dims).
//
// A single shadow tap per froxel resolves canopy detail far finer than the grid (a leaf gap is a fraction
// of a 12 px cell at 50 m), so which cells read "lit" depends on exactly where the sample lands - and every
// camera move moves the grid. The same holds for the wind noise in the fog density. Jittering the sample and
// averaging through the temporal history hides that at rest but smears and lags while moving. The box filter
// is the value the cell should have; it changes only as fast as the cell actually slides through the shadow
// and the noise, so this volume needs no history at all.
//
// The shadow comes from the fog's own coarse shadow map when there is one (FogShadowValid): the cascades
// resolve single leaves, far finer than these taps can sample, so with them the taps hit a different random set
// of leaf gaps after every step and the shafts rearranged while walking. Without it, one cascade per cell,
// chosen by the cell centre's view depth. Taps outside the map count as lit.
float3 CellSunScattering(uint3 id)
{
    float2 cell = 1.0f / float2(GridWidth, GridHeight);
    float2 uv = (float2(id.xy) + 0.5f) * cell;
    float centerDepth = SliceToDepth((float(id.z) + 0.5f) / float(GridDepth));

    uint cascade = 0;
    if (CascadeCount > 0)
    {
        cascade = CascadeCount - 1;
        for (uint c = 0; c < CascadeCount; ++c)
        {
            if (centerDepth <= CascadeSplitDistances[c])
            {
                cascade = c;
                break;
            }
        }
    }

    float3 rayPerDepth = RayPerDepth(uv);
    float3 cellX = RayPerDepth(uv + float2(cell.x * 0.5f, 0.0f)) - RayPerDepth(uv - float2(cell.x * 0.5f, 0.0f));
    float3 cellY = RayPerDepth(uv + float2(0.0f, cell.y * 0.5f)) - RayPerDepth(uv - float2(0.0f, cell.y * 0.5f));

    float4x4 m = FogShadowValid != 0 ? FogShadowViewProj : CascadeViewProj[cascade];
    float3 lightOrigin = mul(float4(CameraPosition, 1.0f), m).xyz;
    float3 lightRay = mul(float4(rayPerDepth, 0.0f), m).xyz;
    float3 lightX = mul(float4(cellX, 0.0f), m).xyz;
    float3 lightY = mul(float4(cellY, 0.0f), m).xyz;

    if (FogShadowValid != 0)
    {
        return SumCellSunTaps(FogShadowMap, true, lightOrigin, lightRay, lightX, lightY, rayPerDepth, cellX, cellY, id);
    }

    bool shadowed = CascadeCount > 0;
    switch (cascade)
    {
        case 0: return SumCellSunTaps(ShadowMapCascade0, shadowed, lightOrigin, lightRay, lightX, lightY, rayPerDepth, cellX, cellY, id);
        case 1: return SumCellSunTaps(ShadowMapCascade1, shadowed, lightOrigin, lightRay, lightX, lightY, rayPerDepth, cellX, cellY, id);
        case 2: return SumCellSunTaps(ShadowMapCascade2, shadowed, lightOrigin, lightRay, lightX, lightY, rayPerDepth, cellX, cellY, id);
        default: return SumCellSunTaps(ShadowMapCascade3, shadowed, lightOrigin, lightRay, lightX, lightY, rayPerDepth, cellX, cellY, id);
    }
}

// World-space bounding sphere (xyz center, w radius) of one 8x8x8 froxel block: the sphere around the
// block's eight corners between its first and last depth-slice boundary. Every jittered sample of the
// block lies inside the convex hull of these corners.
float4 FroxelBlockSphere(uint3 groupId)
{
    uint3 first = groupId * FOG_GROUP_SIZE;
    uint3 last = min(first + FOG_GROUP_SIZE, uint3(GridWidth, GridHeight, GridDepth));

    float2 uvMin = float2(first.xy) / float2(GridWidth, GridHeight);
    float2 uvMax = float2(last.xy) / float2(GridWidth, GridHeight);
    float nearDepth = SliceToDepth(float(first.z) / float(GridDepth));
    float farDepth = SliceToDepth(float(last.z) / float(GridDepth));

    float3 corners[8];
    [unroll]
    for (uint i = 0; i < 8; ++i)
    {
        float2 uv = float2((i & 1) ? uvMax.x : uvMin.x, (i & 2) ? uvMax.y : uvMin.y);
        float depth = (i & 4) ? farDepth : nearDepth;
        corners[i] = FogWorldPosition(FogWorldRay(uv), depth);
    }

    float3 center = float3(0.0f, 0.0f, 0.0f);
    [unroll]
    for (uint j = 0; j < 8; ++j)
    {
        center += corners[j];
    }
    center *= 0.125f;

    float radiusSquared = 0.0f;
    [unroll]
    for (uint k = 0; k < 8; ++k)
    {
        float3 offset = corners[k] - center;
        radiusSquared = max(radiusSquared, dot(offset, offset));
    }

    return float4(center, sqrt(radiusSquared));
}

// Radiance scattered toward the camera by the block's lights. ray: camera-to-froxel direction.
float3 BlockLightScattering(float3 worldPos, float3 ray, uint blockLightCount)
{
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    for (uint i = 0; i < blockLightCount; ++i)
    {
        Light light = Lights[s_blockLights[i]];
        float3 toFroxel = worldPos - light.Position;
        float distance = length(toFroxel);
        float attenuation = LightAttenuation(distance, light.Range);
        if (attenuation <= 0.0f)
        {
            continue;
        }

        float3 lightToFroxel = toFroxel / max(distance, 1e-4f);
        if (light.Type == LIGHT_TYPE_SPOT)
        {
            attenuation *= SpotFactor(light, lightToFroxel);
        }

        // Light travelling along lightToFroxel scatters toward the camera (-ray).
        sum += light.Color * (light.Intensity * attenuation * light.FogScattering) * ScatterPhase(dot(ray, -lightToFroxel));
    }

    return sum * LightScatterStrength;
}

// Adds this froxel's local fog volume density: sigma (extinction) and sigma * colour (scattering).
void AccumulateFogVolumes(float3 worldPos, uint blockVolumeCount, inout float sigmaTotal, inout float3 sigmaScatter)
{
    for (uint i = 0; i < blockVolumeCount; ++i)
    {
        FogVolume volume = FogVolumes[s_blockVolumes[i]];
        float3 local = FogVolumeToLocal(volume, worldPos);
        float distance = FogVolumeShapeDistance(volume, local);
        if (distance >= 1.0f)
        {
            continue;
        }

        float detail = volume.NoiseDetail;
        float3 noiseUvw = float3(worldPos.x * detail / NoiseSize - WindOffset.x * detail, worldPos.y * detail / NoiseSize, worldPos.z * detail / NoiseSize - WindOffset.y * detail);
        float noise = NoiseVolume.SampleLevel(NoiseSampler, noiseUvw, 0.0f);

        float sigma = volume.Density * FogVolumeEdgeFade(distance, volume.EdgeFade) * FogVolumeHeightFactor(volume, local) * NoiseDensityFactor(noise, volume.NoiseAmount);
        sigmaTotal += sigma;
        sigmaScatter += sigma * volume.Color;
    }
}

[numthreads(FOG_GROUP_SIZE, FOG_GROUP_SIZE, FOG_GROUP_SIZE)]
void main(uint3 id : SV_DispatchThreadID, uint3 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex)
{
    // 1. Cull the frame's lights and fog volumes against this block once, in parallel. Every thread of
    //    the group must reach both barriers, so the grid bounds check comes after them.
    if (groupIndex == 0)
    {
        s_blockLightHits = 0;
        s_blockVolumeHits = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    float4 blockSphere = FroxelBlockSphere(groupId);
    for (uint lightIndex = groupIndex; lightIndex < LightCount; lightIndex += FOG_GROUP_THREADS)
    {
        if (LightReachesFogSphere(Lights[lightIndex], blockSphere))
        {
            uint slot;
            InterlockedAdd(s_blockLightHits, 1, slot);
            if (slot < MAX_BLOCK_LIGHTS)
            {
                s_blockLights[slot] = lightIndex;
            }
        }
    }
    for (uint volumeIndex = groupIndex; volumeIndex < FogVolumeCount; volumeIndex += FOG_GROUP_THREADS)
    {
        FogVolume volume = FogVolumes[volumeIndex];
        float3 toCenter = volume.Center - blockSphere.xyz;
        float reach = length(volume.HalfSize) + blockSphere.w;
        if (dot(toCenter, toCenter) <= reach * reach)
        {
            uint slot;
            InterlockedAdd(s_blockVolumeHits, 1, slot);
            if (slot < MAX_BLOCK_VOLUMES)
            {
                s_blockVolumes[slot] = volumeIndex;
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (id.x >= GridWidth || id.y >= GridHeight || id.z >= GridDepth)
    {
        return;
    }

    uint blockLightCount = min(s_blockLightHits, MAX_BLOCK_LIGHTS);

    if (DebugMode == 4)
    {
        // Lights per block: black none, blue one .. yellow sixteen or more, red over capacity.
        float3 color = float3(0.0f, 0.0f, 0.0f);
        if (s_blockLightHits > MAX_BLOCK_LIGHTS)
        {
            color = float3(1.0f, 0.0f, 0.0f);
        }
        else if (blockLightCount > 0)
        {
            color = lerp(float3(0.0f, 0.2f, 1.0f), float3(1.0f, 1.0f, 0.0f), saturate(float(blockLightCount - 1) / 15.0f));
        }

        const float debugSigma = 0.02f;
        InjectOutput[id] = float4(color * debugSigma, debugSigma);
        InjectSunOutput[id] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    // A cell that starts behind the farthest surface seen by every pixel that samples it (its own column and,
    // through the composite's bilinear lookup, its neighbours) never shows its sun term: skip its shadow taps.
    // Behind the ground or a tree trunk that is most of the grid.
    float farDepth = 0.0f;
    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            int2 column = clamp(int2(id.xy) + int2(dx, dy), int2(0, 0), int2(GridWidth, GridHeight) - 1);
            farDepth = max(farDepth, ColumnFarDepth.Load(int4(column, 0, 0)).r);
        }
    }
    // The half-float far depth may round down by a few centimetres; the margin keeps the test conservative.
    bool hiddenCell = (DiagFlags & 1u) == 0 && SliceToDepth(float(id.z) / float(GridDepth)) > farDepth * 1.002f;

    // 2. This froxel. Visible cells take their zone fog density from the same fixed taps as the sun (cell
    //    average, see CellSunScattering): a single sample per cell, jittered and blended through the history,
    //    made the extinction - and with it every shaft behind it - change noisily from frame to frame while
    //    walking. Hidden cells (behind every surface) keep the cheap single sample.
    float2 uv = (float2(id.xy) + ScreenJitter) / float2(GridWidth, GridHeight);
    float3 ray = FogWorldRay(uv);
    float viewDepth = SliceToDepth((float(id.z) + Jitter) / float(GridDepth));
    float3 worldPos = FogWorldPosition(ray, viewDepth);

    float3 sun = hiddenCell ? float3(0.0f, 0.0f, 0.0f) : CellSunScattering(id);
    float sigma = hiddenCell ? ZoneSigmaAt(worldPos) : sun.z;

    // The sun's phase function is NOT applied here: it depends on the view direction, and the temporal
    // history would keep it as seen from where the camera used to be. Only the view-independent parts go
    // into the volumes; CS_FogIntegrate applies the sun phase for the current rays.
    float3 radiance = FogTint + BlockLightScattering(worldPos, ray, blockLightCount);

    uint blockVolumeCount = min(s_blockVolumeHits, MAX_BLOCK_VOLUMES);
    float sigmaTotal = sigma;
    float3 sigmaScatter = sigma.xxx;
    AccumulateFogVolumes(worldPos, blockVolumeCount, sigmaTotal, sigmaScatter);

    InjectOutput[id] = float4(radiance * sigmaScatter, sigmaTotal);

    // 3. The sun's shadowed scattering: the zone fog's part box-filtered over the cell with fixed taps; local fog
    //    volumes from the froxel sample, scaled by the cell's mean visibility.
    float3 sunScatter = float3(0.0f, 0.0f, 0.0f);
    if (!hiddenCell)
    {
        sunScatter = sun.x.xxx + (sigmaScatter - sigma.xxx) * sun.y;
    }

    InjectSunOutput[id] = (DiagFlags & 2u) != 0 ? float4(0.0f, 0.0f, 0.0f, 0.0f) : float4(sunScatter, 0.0f);
}