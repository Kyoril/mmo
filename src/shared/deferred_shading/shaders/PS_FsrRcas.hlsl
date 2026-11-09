// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// FSR 1 RCAS (Robust Contrast Adaptive Sharpening): the sharpening half of AMD FidelityFX Super
// Resolution 1.0, run at output resolution on the PS_FsrEasu result. Plain float HLSL for the same
// reasons as PS_FsrEasu.
//
// A 5-tap cross. The sharpening lobe is the largest one that cannot push any channel outside
// [0, 1] given the neighbourhood's min and max, so it never clips; on noisy areas it backs off.
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

// Upper bound of the negative lobe, so the kernel stays well clear of unstable weights.
static const float RcasLimit = 0.25f - (1.0f / 16.0f);

float4 main(PS_INPUT input) : SV_TARGET
{
    // Source and output have the same size here.
    const int2 sp = int2(input.Position.xy);

    //      b
    //    d e f
    //      h
    const float3 b = LoadSource(sp + int2(0, -1));
    const float3 d = LoadSource(sp + int2(-1, 0));
    const float3 e = LoadSource(sp);
    const float3 f = LoadSource(sp + int2(1, 0));
    const float3 h = LoadSource(sp + int2(0, 1));

    // Noise detection: a centre far off its neighbours' average, relative to the local range, is
    // noise rather than detail, and is sharpened less.
    const float bL = FsrLuma(b);
    const float dL = FsrLuma(d);
    const float eL = FsrLuma(e);
    const float fL = FsrLuma(f);
    const float hL = FsrLuma(h);
    const float lumaMax = max(max(max(bL, dL), max(eL, fL)), hL);
    const float lumaMin = min(min(min(bL, dL), min(eL, fL)), hL);
    float nz = 0.25f * (bL + dL + fL + hL) - eL;
    nz = saturate(abs(nz) / max(lumaMax - lumaMin, 1.0f / 65536.0f));
    nz = -0.5f * nz + 1.0f;

    // The largest negative lobe that keeps every channel inside [0, 1].
    const float3 mn4 = min(min(min(b, d), min(f, h)), e);
    const float3 mx4 = max(max(max(b, d), max(f, h)), e);
    const float3 hitMin = mn4 / max(4.0f * mx4, 1.0f / 65536.0f);
    const float3 hitMax = (1.0f - mx4) / min(4.0f * mn4 - 4.0f, -1.0f / 65536.0f);
    const float3 lobeRgb = max(-hitMin, hitMax);
    float lobe = max(-RcasLimit, min(max(max(lobeRgb.r, lobeRgb.g), lobeRgb.b), 0.0f)) * Sharpness;
    lobe *= nz;

    // Normalised 5-tap filter with the lobe on the four neighbours.
    const float3 color = (lobe * (b + d + f + h) + e) / (4.0f * lobe + 1.0f);
    return float4(saturate(color), 1.0f);
}
