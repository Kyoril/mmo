// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
    float2 TexCoord : TEXCOORD0;
};

// G-Buffer normal target: rgb = world normal * 0.5 + 0.5, a = linear RADIAL depth
// (length(viewPos)), not view-space Z.
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

float4 main(PS_INPUT input) : SV_TARGET
{
    // STUB: constant occlusion so the plumbing can be verified before the real math lands.
    return 0.5f;
}
