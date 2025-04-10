// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

struct VertexIn
{
    float4 pos : POSITION;
#ifdef WITH_COLOR
	float4 color : COLOR;
#endif
#ifdef WITH_NORMAL
	float3 normal : NORMAL;
#endif
#ifdef WITH_BINORMAL
	float3 binormal : BINORMAL;
#endif
#ifdef WITH_TANGENT
	float3 tangent : TANGENT;
#endif
#ifdef WITH_TEX
	float2 uv0 : TEXCOORD0;
#if WITH_TEX > 1
	float2 uv1 : TEXCOORD1;
#endif
#if WITH_TEX > 2
	float2 uv2 : TEXCOORD2;
#endif
#if WITH_TEX > 3
	float2 uv3 : TEXCOORD3;
#endif
#if WITH_TEX > 4
	float2 uv4 : TEXCOORD4;
#endif
#endif
};

struct VertexOut
{
	float4 pos : SV_POSITION;
#ifdef WITH_COLOR
	float4 color : COLOR;
#endif
#ifdef WITH_NORMAL
	float3 normal : NORMAL;
#endif
#ifdef WITH_BINORMAL
	float3 binormal : BINORMAL;
#endif
#ifdef WITH_TANGENT
	float3 tangent : TANGENT;
#endif
#ifdef WITH_TEX
	float2 uv0 : TEXCOORD0;
#if WITH_TEX > 1
	float2 uv1 : TEXCOORD1;
#endif
#if WITH_TEX > 2
	float2 uv2 : TEXCOORD2;
#endif
#if WITH_TEX > 3
	float2 uv3 : TEXCOORD3;
#endif
#if WITH_TEX > 4
	float2 uv4 : TEXCOORD4;
#endif
#endif
};
