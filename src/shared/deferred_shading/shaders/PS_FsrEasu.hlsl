// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// FSR 1 EASU (Edge Adaptive Spatial Upsampling): the upscale half of AMD FidelityFX Super
// Resolution 1.0, rewritten as plain float HLSL so fxc and the glslang/SPIRV-Cross Metal path both
// take it unchanged. PS_FsrRcas sharpens the result afterwards.
//
// Per output pixel it reads the 12 source texels around it, estimates the local edge direction and
// strength from luma, and filters with a Lanczos-like kernel stretched along that edge. The result
// is clamped to the nearest 2x2 texels, so the kernel's negative lobes cannot ring.
//
// Derived from ffx_fsr1.h, which is under this licence:
//
// Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "UpscaleCommon.hlsli"

// Accumulates direction and edge length from one '+' of luma around lC, weighted by how close the
// output position is to that '+' (bilinear weight w).
//      a
//    b c d
//      e
void EasuSet(inout float2 dir, inout float len, float w, float lA, float lB, float lC, float lD, float lE)
{
    // Horizontal: gradient across the centre, normalised by the larger one-sided step. An edge
    // gives a full-length step on one side; a thin line gives steps on both sides that cancel.
    const float dc = lD - lC;
    const float cb = lC - lB;
    const float dirX = lD - lB;
    float lenX = saturate(abs(dirX) / max(max(abs(dc), abs(cb)), 1.0f / 65536.0f));
    lenX *= lenX;
    dir.x += dirX * w;
    len += lenX * w;

    // The same along the vertical axis.
    const float ec = lE - lC;
    const float ca = lC - lA;
    const float dirY = lE - lA;
    float lenY = saturate(abs(dirY) / max(max(abs(ec), abs(ca)), 1.0f / 65536.0f));
    lenY *= lenY;
    dir.y += dirY * w;
    len += lenY * w;
}

// Adds one texel with the edge-aligned approximate Lanczos 2 weight.
void EasuTap(inout float3 aC, inout float aW, float2 off, float2 dir, float2 len2, float lob, float clp, float3 c)
{
    // Rotate the offset into the edge's frame and scale it: stretched along the edge, squeezed across.
    float2 v;
    v.x = off.x * dir.x + off.y * dir.y;
    v.y = off.x * (-dir.y) + off.y * dir.x;
    v *= len2;

    // Squared distance, clipped to the kernel window.
    const float d2 = min(v.x * v.x + v.y * v.y, clp);

    // (25/16 * (2/5 * x^2 - 1)^2 - (25/16 - 1)) * (lob * x^2 - 1)^2, a polynomial stand-in for Lanczos 2.
    float wB = 2.0f / 5.0f * d2 - 1.0f;
    float wA = lob * d2 - 1.0f;
    wB *= wB;
    wA *= wA;
    wB = 25.0f / 16.0f * wB - (25.0f / 16.0f - 1.0f);
    const float w = wB * wA;

    aC += c * w;
    aW += w;
}

float4 main(PS_INPUT input) : SV_TARGET
{
    // Position in source texel space with texel centres on integers; f is the texel up-left of it.
    float2 pp = input.Position.xy * SourcePerOutput - 0.5f;
    const float2 fp = floor(pp);
    pp -= fp;
    const int2 ip = int2(fp);

    // The 12-tap footprint:
    //      b c
    //    e f g h
    //    i j k l
    //      n o
    const float3 b = LoadSource(ip + int2(0, -1));
    const float3 c = LoadSource(ip + int2(1, -1));
    const float3 e = LoadSource(ip + int2(-1, 0));
    const float3 f = LoadSource(ip);
    const float3 g = LoadSource(ip + int2(1, 0));
    const float3 h = LoadSource(ip + int2(2, 0));
    const float3 i = LoadSource(ip + int2(-1, 1));
    const float3 j = LoadSource(ip + int2(0, 1));
    const float3 k = LoadSource(ip + int2(1, 1));
    const float3 l = LoadSource(ip + int2(2, 1));
    const float3 n = LoadSource(ip + int2(0, 2));
    const float3 o = LoadSource(ip + int2(1, 2));

    const float bL = FsrLuma(b);
    const float cL = FsrLuma(c);
    const float eL = FsrLuma(e);
    const float fL = FsrLuma(f);
    const float gL = FsrLuma(g);
    const float hL = FsrLuma(h);
    const float iL = FsrLuma(i);
    const float jL = FsrLuma(j);
    const float kL = FsrLuma(k);
    const float lL = FsrLuma(l);
    const float nL = FsrLuma(n);
    const float oL = FsrLuma(o);

    // Direction and edge length, bilinearly blended from the four '+' shapes centred on f, g, j, k.
    float2 dir = 0.0f;
    float len = 0.0f;
    EasuSet(dir, len, (1.0f - pp.x) * (1.0f - pp.y), bL, eL, fL, gL, jL);
    EasuSet(dir, len, pp.x * (1.0f - pp.y),          cL, fL, gL, hL, kL);
    EasuSet(dir, len, (1.0f - pp.x) * pp.y,          fL, iL, jL, kL, nL);
    EasuSet(dir, len, pp.x * pp.y,                   gL, jL, kL, lL, oL);

    // Normalise the direction; a flat area has none, so fall back to the x axis.
    const float dirR2 = dir.x * dir.x + dir.y * dir.y;
    const bool zero = dirR2 < (1.0f / 32768.0f);
    dir = zero ? float2(1.0f, 0.0f) : dir * rsqrt(dirR2);

    // len is in [0, 2] after the four '+' contributions; map it to [0, 1] and shape it.
    len = len * 0.5f;
    len *= len;

    // Diagonal edges stretch further (up to sqrt(2)) so the kernel reaches the next texel along them.
    const float stretch = (dir.x * dir.x + dir.y * dir.y) / max(abs(dir.x), abs(dir.y));
    const float2 len2 = float2(1.0f + (stretch - 1.0f) * len, 1.0f - 0.5f * len);

    // Negative lobe strength: none in flat areas, strong on clean edges.
    const float lob = 0.5f + ((1.0f / 4.0f - 0.04f) - 0.5f) * len;
    const float clp = 1.0f / lob;

    float3 aC = 0.0f;
    float aW = 0.0f;
    EasuTap(aC, aW, float2( 0.0f, -1.0f) - pp, dir, len2, lob, clp, b);
    EasuTap(aC, aW, float2( 1.0f, -1.0f) - pp, dir, len2, lob, clp, c);
    EasuTap(aC, aW, float2(-1.0f,  1.0f) - pp, dir, len2, lob, clp, i);
    EasuTap(aC, aW, float2( 0.0f,  1.0f) - pp, dir, len2, lob, clp, j);
    EasuTap(aC, aW, float2( 0.0f,  0.0f) - pp, dir, len2, lob, clp, f);
    EasuTap(aC, aW, float2(-1.0f,  0.0f) - pp, dir, len2, lob, clp, e);
    EasuTap(aC, aW, float2( 1.0f,  1.0f) - pp, dir, len2, lob, clp, k);
    EasuTap(aC, aW, float2( 2.0f,  1.0f) - pp, dir, len2, lob, clp, l);
    EasuTap(aC, aW, float2( 2.0f,  0.0f) - pp, dir, len2, lob, clp, h);
    EasuTap(aC, aW, float2( 1.0f,  0.0f) - pp, dir, len2, lob, clp, g);
    EasuTap(aC, aW, float2( 1.0f,  2.0f) - pp, dir, len2, lob, clp, o);
    EasuTap(aC, aW, float2( 0.0f,  2.0f) - pp, dir, len2, lob, clp, n);

    // Deringing: never leave the range of the 2x2 texels around the position.
    const float3 min4 = min(min(f, g), min(j, k));
    const float3 max4 = max(max(f, g), max(j, k));
    const float3 color = min(max4, max(min4, aC / aW));

    return float4(color, 1.0f);
}
