// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Screen-space ambient occlusion using the visibility-bitmask occlusion core of SSILVB
// (Therrien, Levesque & Gilet, 2023).
//
// Rather than tracking a single maximum horizon angle per slice — which models every occluder
// as an infinitely thick heightfield and over-darkens behind thin geometry — each slice keeps a
// 32-bit mask whose bits are angular sectors. A sample sets only the sectors it actually blocks,
// bounded by an assumed occluder Thickness, so light correctly passes behind railings and
// foliage. Occlusion is then popcount(mask) / 32.

static const float PI = 3.14159265359f;

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
    float2 TexCoord : TEXCOORD0;
};

// G-Buffer normal target: rgb = world normal * 0.5 + 0.5, a = linear RADIAL depth
// (length(viewPos)), not view-space Z. Reconstruct positions with viewRay * depth.
Texture2D NormalTexture : register(t1);
SamplerState PointSampler : register(s0);

// Per-view matrices (b12). Auto-bound by GraphicsDeviceD3D11 (kPerViewMatrixBufferSlot).
cbuffer ViewMatrices : register(b12)
{
    column_major matrix matView;
    column_major matrix matProj;
    column_major matrix matInvView;
    column_major matrix InverseProjection;
};

cbuffer SsaoBuffer : register(b2)
{
    float2 ScreenSize;
    float2 InvScreenSize;
    float Radius;
    float Intensity;
    float Thickness;
    uint SliceCount;
    uint StepCount;
    uint DebugMode;
    float2 SsaoPadding;
};

// Reconstructs a view-space position from a screen UV and the RADIAL depth stored in the
// normal target's alpha. Mirrors PS_DeferredLighting.hlsl so the two passes cannot disagree.
float3 ReconstructViewPos(float2 uv, float radialDepth)
{
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 viewH = mul(float4(ndc, 1.0f, 1.0f), InverseProjection);
    float3 viewRay = normalize(viewH.xyz / viewH.w);
    return viewRay * radialDepth;
}

// Interleaved gradient noise, keyed on pixel position ONLY — deliberately not on a frame index.
// There is no TAA in this engine, so a frame-varying pattern would shimmer with nothing to
// resolve it. The fixed pattern is instead resolved spatially by the bilateral blur pass.
float InterleavedGradientNoise(float2 pixelCoord)
{
    return frac(52.9829189f * frac(dot(pixelCoord, float2(0.06711056f, 0.00583715f))));
}

// Returns a mask of the sectors covered by the angular range [minAngle, maxAngle], where both
// angles are in [0, PI] measured from the view direction within the slice plane.
uint SectorMask(float minAngle, float maxAngle)
{
    float startF = saturate(minAngle / PI);
    float endF = saturate(maxAngle / PI);

    uint startBit = (uint) floor(startF * 32.0f);
    uint bitCount = (uint) ceil((endF - startF) * 32.0f);

    if (bitCount == 0u)
    {
        return 0u;
    }

    // HLSL shifts are taken mod 32, so both operands must be guarded explicitly: a shift of 32
    // would wrap to a shift of 0 and silently produce the wrong mask.
    startBit = min(startBit, 31u);
    bitCount = min(bitCount, 32u);

    uint mask = (bitCount >= 32u) ? 0xFFFFFFFFu : ((1u << bitCount) - 1u);
    return mask << startBit;
}

float4 main(PS_INPUT input) : SV_TARGET
{
    float4 normalData = NormalTexture.SampleLevel(PointSampler, input.TexCoord, 0);
    float depth = normalData.a;

    // Sky and other pixels the G-Buffer never wrote have depth 0 (the target is cleared to
    // zero). They are fully unoccluded.
    if (depth <= 0.0f)
    {
        return 1.0f;
    }

    float3 viewPos = ReconstructViewPos(input.TexCoord, depth);
    float3 worldNormal = normalize(normalData.rgb * 2.0f - 1.0f);
    float3 viewNormal = normalize(mul((float3x3) matView, worldNormal));
    float3 viewDir = normalize(-viewPos);

    // Project the world-space radius into a screen-space (UV) radius at this depth.
    // matProj[0][0] scales view-space X into NDC X, and NDC spans 2 units across the screen,
    // hence the 0.5 to land in UV space.
    float projScale = matProj[0][0];
    float screenRadius = (Radius * projScale * 0.5f) / max(depth, 0.0001f);

    // Clamp so a near-camera pixel cannot march across the whole screen, which would both
    // destroy the cache and undersample badly.
    screenRadius = min(screenRadius, 0.15f);

    float noise = InterleavedGradientNoise(input.Position.xy);

    float occlusion = 0.0f;
    float validSlices = 0.0f;

    [loop]
    for (uint slice = 0u; slice < SliceCount; ++slice)
    {
        float phi = (float(slice) + noise) * PI / float(SliceCount);
        float2 sliceDir = float2(cos(phi), sin(phi));

        // The slice plane contains the view direction and the slice's screen-space direction
        // lifted into view space. This is the standard GTAO-family approximation.
        float3 sliceDir3 = float3(sliceDir, 0.0f);
        float3 planeNormal = cross(sliceDir3, viewDir);
        float planeNormalLen = length(planeNormal);

        if (planeNormalLen < 0.0001f)
        {
            continue;
        }

        planeNormal /= planeNormalLen;

        // Project the surface normal into the slice plane.
        float3 projNormal = viewNormal - planeNormal * dot(viewNormal, planeNormal);
        float projNormalLen = length(projNormal);

        if (projNormalLen < 0.0001f)
        {
            continue;
        }

        projNormal /= projNormalLen;

        // Signed angle of the projected normal relative to the view direction, within the plane.
        float nAngle = acos(clamp(dot(projNormal, viewDir), -1.0f, 1.0f));
        float nSign = (dot(cross(viewDir, projNormal), planeNormal) < 0.0f) ? -1.0f : 1.0f;
        nAngle *= nSign;

        // Only the hemisphere in front of the surface can occlude it. Sectors outside
        // [n - PI/2, n + PI/2] lie behind the surface and must not count — this is the
        // normal-oriented weighting, done by masking rather than a per-sector cosine.
        uint validMask = SectorMask(max(nAngle - PI * 0.5f, 0.0f), min(nAngle + PI * 0.5f, PI));
        uint validCount = countbits(validMask);

        if (validCount == 0u)
        {
            continue;
        }

        uint bitmask = 0u;

        [loop]
        for (uint side = 0u; side < 2u; ++side)
        {
            float2 dir = (side == 0u) ? sliceDir : -sliceDir;

            [loop]
            for (uint step = 0u; step < StepCount; ++step)
            {
                // Jittered, linearly increasing march distance.
                float t = (float(step) + 1.0f + noise) / float(StepCount);
                float2 sampleUv = input.TexCoord + dir * t * screenRadius;

                if (any(sampleUv < 0.0f) || any(sampleUv > 1.0f))
                {
                    break;
                }

                float sampleDepth = NormalTexture.SampleLevel(PointSampler, sampleUv, 0).a;

                if (sampleDepth <= 0.0f)
                {
                    continue;
                }

                float3 samplePos = ReconstructViewPos(sampleUv, sampleDepth);
                float3 delta = samplePos - viewPos;
                float dist = length(delta);

                if (dist < 0.0001f || dist > Radius)
                {
                    continue;
                }

                float3 frontDir = delta / dist;

                // The back of the occluder: the sample pushed away from the camera by the
                // assumed Thickness. The sectors between the front and back angles are what
                // this sample actually blocks — this bounded range is the whole point of the
                // technique, and what stops a thin railing occluding like a solid wall.
                float3 backDir = normalize(delta - viewDir * Thickness);

                float angFront = acos(clamp(dot(frontDir, viewDir), -1.0f, 1.0f));
                float angBack = acos(clamp(dot(backDir, viewDir), -1.0f, 1.0f));

                bitmask |= SectorMask(min(angFront, angBack), max(angFront, angBack));
            }
        }

        // Discard sectors behind the surface, then measure what fraction of the sectors that
        // *could* occlude actually do.
        bitmask &= validMask;
        occlusion += float(countbits(bitmask)) / float(validCount);
        validSlices += 1.0f;
    }

    if (validSlices < 1.0f)
    {
        return 1.0f;
    }

    float ao = saturate(1.0f - occlusion / validSlices);
    ao = pow(ao, Intensity);

    return ao;
}
