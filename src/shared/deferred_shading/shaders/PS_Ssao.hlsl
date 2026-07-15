// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Screen-space ambient occlusion using the visibility-bitmask occlusion core of SSILVB
// (Therrien, Levesque & Gilet, 2023).
//
// Rather than tracking a single maximum horizon angle per slice — which models every occluder
// as an infinitely thick heightfield and over-darkens behind thin geometry — each slice keeps a
// 32-bit mask whose bits are angular sectors. A sample sets only the sectors it actually blocks,
// bounded by an assumed occluder Thickness, so light correctly passes behind railings and
// foliage. Slice occlusion is then countbits(mask & validMask) / countbits(validMask).

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
// angles are SIGNED, in [-PI/2, +PI/2], measured from the view direction within the slice plane
// and signed by the slice tangent (see sliceTangent in main).
//
// The signed parametrization is load-bearing, and an unsigned [0, PI] one (as produced by a bare
// acos) is silently, badly wrong here — do not "simplify" it back:
//   * acos discards which side of the view direction a sample sits on, so an occluder at +0.8 and
//     one at -0.8 collapse onto the same bit. A one-sided occluder (the base of a wall, the side
//     of a rock — exactly what this pass exists to darken) then occludes both sides, ~2x too dark.
//   * validMask is derived from the projected normal's angle, which is inherently signed. Mixing
//     an unsigned sample parametrization with a signed valid range makes a flat, empty, tilted
//     plane self-occlude: coplanar samples land strictly inside validMask instead of on its edge,
//     and bare ground reads AO ~0.85 instead of 1.0 — a grey wash that breathes with the camera.
// With this mapping, the hemisphere above the surface is exactly [n - PI/2, n + PI/2] and a
// coplanar sample's horizon lands exactly on the validMask boundary, contributing nothing.
uint SectorMask(float minAngle, float maxAngle)
{
    // Map the signed range [-PI/2, +PI/2] onto the sector fraction [0, 1]; the view direction
    // therefore sits at the centre of the mask (fraction 0.5, bit 16).
    float startF = saturate((minAngle + PI * 0.5f) / PI);
    float endF = saturate((maxAngle + PI * 0.5f) / PI);

    // Quantize both edges to the NEAREST sector boundary (i.e. a sector counts when its centre is
    // covered), rather than floor-ing the start and ceil-ing the count. Both of those round
    // OUTWARD, which inflates every range by up to two sectors and, worse, makes two ranges that
    // share a boundary each claim the boundary sector. A coplanar sample's horizon lands exactly
    // on the validMask edge, so that double-claim put a spurious bit inside validMask and left
    // flat ground at AO ~0.96 instead of 1.0. Round-to-nearest tiles adjacent ranges exactly and
    // is unbiased; sub-sector-width occluders are then handled by Thickness, not by rounding.
    uint startBit = (uint) floor(startF * 32.0f + 0.5f);
    uint endBit = (uint) floor(endF * 32.0f + 0.5f);

    if (endBit <= startBit)
    {
        return 0u;
    }

    uint bitCount = endBit - startBit;

    // HLSL shifts are taken mod 32, so both operands must be guarded explicitly: a shift of 32
    // would wrap to a shift of 0 and silently produce the wrong mask. startBit == 32 cannot reach
    // here (it would force endBit <= startBit and hit the early-out above), and the bitCount >= 32
    // ternary is what keeps the 1u << 32 case from wrapping to 1u << 0.
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

    // This engine is row-vector: the vector goes FIRST. MakeViewMatrix writes translation into the
    // last column and row-major C++ storage read back through `column_major` transposes it, so
    // mul(matView, v) would apply the view->world rotation to a world normal — geometrically
    // meaningless, and it fails silently because the result is still unit length. See the proven
    // mul(float4(viewPos, 1.0), matInvView) in PS_DeferredLighting.hlsl.
    float3 viewNormal = normalize(mul(worldNormal, (float3x3) matView));
    float3 viewDir = normalize(-viewPos);

    // Project the world-space radius into a screen-space (UV) radius at this depth.
    //
    // Per-axis, and from view Z rather than radial depth, because both matter:
    //   * MakeProjectionMatrix sets [0][0] = h/aspect and [1][1] = h, so P11 = P00 * aspect.
    //     sliceDir is a unit vector in UV space, so its y component must be scaled by P11; using
    //     P00 for both axes reaches only ~56% as far vertically as horizontally at 16:9.
    //   * The projection identity is uv_offset = r * P * 0.5 / |viewPos.z|. `depth` here is
    //     length(viewPos), which at the screen corners (fovY 45, 16:9) is ~1.31x |z| — so a
    //     radial-depth divide is correct only at the screen centre and ~24% short at the edges.
    // NOTE: this changes the radius projection only. Position reconstruction must keep using
    // radial depth (viewRay * depth) so this pass cannot drift out of alignment with lighting.
    float viewZ = max(abs(viewPos.z), 0.0001f);
    float2 screenRadius = float2(Radius * matProj[0][0], Radius * matProj[1][1]) * 0.5f / viewZ;

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
        //
        // The y flip is required: UV space is y-down (ReconstructViewPos does 1.0 - uv.y * 2.0)
        // while view space is y-up. Feeding the raw UV-space sliceDir into the view-space plane
        // construction builds, for sin(phi) != 0, the MIRRORED plane — so the normal-oriented
        // weighting would be computed for a slice other than the one actually being marched.
        // We keep marching with the unflipped, UV-space sliceDir below.
        float3 sliceDir3 = float3(sliceDir.x, -sliceDir.y, 0.0f);
        float3 planeNormal = cross(sliceDir3, viewDir);
        float planeNormalLen = length(planeNormal);

        if (planeNormalLen < 0.0001f)
        {
            continue;
        }

        planeNormal /= planeNormalLen;

        // An explicit in-plane tangent, perpendicular to viewDir, gives every in-plane direction a
        // robust sign: theta = atan2(dot(d, sliceTangent), dot(d, viewDir)) is signed directly,
        // with viewDir at theta = 0. This replaces the fragile acos + cross-sign construction and
        // is what lets the sample angles and the valid range share one parametrization.
        float3 sliceTangent = normalize(cross(planeNormal, viewDir));

        // Project the surface normal into the slice plane.
        float3 projNormal = viewNormal - planeNormal * dot(viewNormal, planeNormal);
        float projNormalLen = length(projNormal);

        if (projNormalLen < 0.0001f)
        {
            continue;
        }

        projNormal /= projNormalLen;

        // Signed angle of the projected normal relative to the view direction, within the plane.
        float nAngle = atan2(dot(projNormal, sliceTangent), dot(projNormal, viewDir));

        // Only the hemisphere in front of the surface can occlude it. Sectors outside
        // [n - PI/2, n + PI/2] lie behind the surface and must not count — this is the
        // normal-oriented weighting, done by masking rather than a per-sector cosine. The range is
        // intersected with the representable range [-PI/2, +PI/2].
        //
        // Unlike the old unsigned [0, PI] version this is never empty for a surface facing the
        // camera, so slices are no longer silently dropped by the validCount == 0 early-out for
        // nAngle near -PI/2 while their mirror at +PI/2 stays fully weighted — that asymmetry
        // biased the subset of slices that validSlices averaged over.
        uint validMask = SectorMask(max(nAngle - PI * 0.5f, -PI * 0.5f), min(nAngle + PI * 0.5f, PI * 0.5f));
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
                // Jittered, linearly increasing march distance. t stays within [0, 1] so the march
                // never overshoots screenRadius; the dist < 0.0001f guard below rejects the
                // degenerate self-sample when step 0 lands on the shading point at noise ~0.
                float t = (float(step) + noise) / float(StepCount);
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

                // Signed, in the same parametrization as nAngle and validMask, so which side of
                // the view direction the occluder is on is preserved. `side` now genuinely
                // separates the two march directions instead of only choosing sampleUv.
                float angFront = atan2(dot(frontDir, sliceTangent), dot(frontDir, viewDir));
                float angBack = atan2(dot(backDir, sliceTangent), dot(backDir, viewDir));

                // A sample can sit behind the shading point relative to the view (|theta| > PI/2).
                // Clamp into the representable range rather than letting the fraction saturate at a
                // wrapped angle: clamping parks it on the mask boundary, where it contributes
                // nothing, which is the correct limit.
                angFront = clamp(angFront, -PI * 0.5f, PI * 0.5f);
                angBack = clamp(angBack, -PI * 0.5f, PI * 0.5f);

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
