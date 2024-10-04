// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#define WITH_COLOR 1
#define WITH_NORMAL 1
#include "VS_InOut.hlsli"

float4 main(VertexIn input) : SV_Target
{
	float4 ambient = float4(0.05, 0.15, 0.25, 1.0);
    float3 lightDir = normalize(-float3(1.0, -0.5, 1.0));
    
    // Calculate the amount of light on this pixel.
    float lightIntensity = saturate(dot(input.normal, lightDir));

    float4 color = float4(saturate(input.color * lightIntensity).xyz, 1.0);

	return (ambient + color);
}
