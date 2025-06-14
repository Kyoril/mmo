// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

static const float PI = 3.14159265359;

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
Texture2D ViewRayTexture : register(t4);

// Cascade shadow maps
Texture2D CascadeShadowMap0 : register(t5);
Texture2D CascadeShadowMap1 : register(t6);
Texture2D CascadeShadowMap2 : register(t7);
Texture2D CascadeShadowMap3 : register(t8);

// Samplers
SamplerState PointSampler : register(s0);
SamplerComparisonState ShadowSampler : register(s1);

// Optimized Poisson disk samples with better distribution for shadow sampling
static const float2 POISSON_DISK[32] = {
    float2(-0.975402, -0.0711386),
    float2(-0.920347, -0.41142),
    float2(-0.883908, 0.217872),
    float2(-0.884518, 0.568041),
    float2(-0.811945, 0.90521),
    float2(-0.792474, -0.779962),
    float2(-0.614856, 0.386578),
    float2(-0.580859, -0.208777),
    float2(-0.53795, 0.716666),
    float2(-0.515427, 0.0899991),
    float2(-0.454634, -0.707938),
    float2(-0.420942, 0.991272),
    float2(-0.261147, 0.588488),
    float2(-0.211219, 0.114841),
    float2(-0.146336, -0.259194),
    float2(-0.139439, -0.888668),
    float2(0.0116886, 0.326395),
    float2(0.0380566, 0.625477),
    float2(0.0625935, -0.50853),
    float2(0.125584, 0.0469069),
    float2(0.169469, -0.997253),
    float2(0.320597, 0.291055),
    float2(0.359172, -0.633717),
    float2(0.435713, -0.250832),
    float2(0.507797, -0.916562),
    float2(0.545763, 0.730216),
    float2(0.56859, 0.11655),
    float2(0.743156, -0.505173),
    float2(0.736442, -0.189734),
    float2(0.843562, 0.357036),
    float2(0.865413, 0.763726),
    float2(0.872005, -0.927)
};

// Bayer matrix for ordered dithering
static const float BAYER_PATTERN[16] = {
    0.0/16.0, 8.0/16.0, 2.0/16.0, 10.0/16.0,
    12.0/16.0, 4.0/16.0, 14.0/16.0, 6.0/16.0,
    3.0/16.0, 11.0/16.0, 1.0/16.0, 9.0/16.0,
    15.0/16.0, 7.0/16.0, 13.0/16.0, 5.0/16.0
};

// Apply dithering to break up color banding
float3 ApplyDithering(float3 color, float2 screenPos)
{
    // Get screen pixel coordinates
    uint x = uint(screenPos.x) % 4;
    uint y = uint(screenPos.y) % 4;
    
    // Add subtle noise based on the bayer pattern
    float bayerValue = BAYER_PATTERN[y * 4 + x];
    
    // The strength of dithering should be proportional to the darkness of the area
    // More aggressive in darker areas, subtler in bright areas
    float ditherStrength = 1.0 / 255.0;  // One step in 8-bit color space
    
    // Apply stronger dithering to darker areas
    ditherStrength *= max(0.5, 1.0 - dot(color, float3(0.299, 0.587, 0.114)));
    
    // Apply the dither
    return color + (bayerValue * 2.0 - 1.0) * ditherStrength;
}

cbuffer Matrices : register(b0)
{
    column_major matrix matWorld;
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
}

// Light structure
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

// Light buffer
cbuffer LightBuffer : register(b2)
{
    uint LightCount;
    float3 AmbientColor;
    Light Lights[16];
}

cbuffer ShadowBuffer : register(b3)
{
    column_major matrix LightViewProj;
    
    // Shadow filter parameters
    float ShadowBias;           // Depth bias to prevent shadow acne
    float NormalBiasScale;      // Bias scale factor based on normal
    float ShadowSoftness;       // Controls general softness of shadows
    float BlockerSearchRadius;  // Search radius for the blocker search phase
    float LightSize;            // Controls the size of the virtual light (larger = softer shadows)
};

// CSM Shadow buffer for cascaded shadow maps
cbuffer CSMShadowBuffer : register(b4)
{
    column_major matrix CascadeViewProj[4];  // Up to 4 cascade view-projection matrices
    float4 CascadeSplits;                    // Split distances (x,y,z,w for cascades 1-4)
    float CSMShadowBias;
    float CSMNormalBiasScale;
    float CSMShadowSoftness;
    float CSMBlockerSearchRadius;
    float CSMLightSize;
    uint CSMCascadeCount;
    float2 CSMPadding;
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

// Calculate adaptive shadow bias based on surface normal and light direction
float CalculateAdaptiveShadowBias(float3 normal, float3 lightDir, float baseBias)
{
    // Calculate the angle between surface normal and light direction
    float NdotL = saturate(dot(normal, lightDir));
    
    // Surfaces facing away from light need less bias
    if (NdotL < 0.1f)
        return baseBias * 0.1f;
    
    // Calculate slope factor - steeper angles need more bias but we want to minimize peter panning
    float slopeFactor = sqrt(1.0f - NdotL * NdotL) / max(NdotL, 0.001f);
    
    // Apply conservative bias scaling to balance acne vs peter panning
    return baseBias * (1.0f + slopeFactor * 0.5f);
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

    float3 specularBRDF = (D * G * F) / (4.0 * max(NdotV * NdotL, 0.001)) * saturate(specular);
    float3 kS = F;
    float3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;

    float3 diffuse = albedo / PI;

    float3 radiance = light.Color * light.Intensity;

    return (kD * diffuse + specularBRDF) * radiance * NdotL * shadow;
}

// CalculateSpotLight function (if needed for spot lights)
float3 CalculateSpotLight(Light light, float3 viewDir, float3 worldPos, float3 normal, float3 albedo, float metallic, float roughness, float specular)
{
    float3 lightToPixel = worldPos - light.Position;
    float distance = length(lightToPixel);

    if (distance > light.Range)
        return float3(0, 0, 0);
    
    float3 lightDir = normalize(-lightToPixel);
    float3 lightDirNorm = normalize(light.Direction);
    
    // Calculate spot light cone attenuation
    float spotEffect = dot(lightDirNorm, -lightDir);
    float spotCutoff = cos(light.SpotAngle * 0.5f);
    
    if (spotEffect < spotCutoff)
        return float3(0, 0, 0);
    
    float3 halfway = normalize(lightDir + viewDir);

    float NdotL = max(dot(normal, lightDir), 0.0);
    float NdotV = max(dot(normal, viewDir), 0.0);
    float NdotH = max(dot(normal, halfway), 0.0);
    float VdotH = max(dot(viewDir, halfway), 0.0);
    
    // Distance attenuation
    float attenuation = pow(1.0 - saturate(distance / light.Range), 2.0);
    
    // Spot light cone attenuation
    float spotAttenuation = saturate((spotEffect - spotCutoff) / (1.0f - spotCutoff));
    
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float3 F = F_Schlick(F0, VdotH);
    float D = D_GGX(NdotH, roughness);
    float G = G_Smith(NdotV, NdotL, roughness);

    float3 specularBRDF = (D * G * F) / (4.0 * max(NdotV * NdotL, 0.001)) * saturate(specular);
    float3 kS = F;
    float3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;

    float3 diffuse = albedo / PI;

    float3 radiance = light.Color * light.Intensity * attenuation * spotAttenuation;

    return (kD * diffuse + specularBRDF) * radiance * NdotL;
}

// Determine which cascade to use for CSM
int GetCascadeIndex(float viewDepth)
{
    // Compare view depth against cascade splits
    if (viewDepth <= CascadeSplits.x) return 0;
    if (viewDepth <= CascadeSplits.y) return 1;
    if (viewDepth <= CascadeSplits.z) return 2;
    if (viewDepth <= CascadeSplits.w) return 3;
    return -1; // Beyond all cascades
}

// Sample the appropriate cascade shadow map
float SampleCascadeShadowMap(int cascadeIndex, float2 uv, float comparison)
{
    switch (cascadeIndex)
    {
        case 0: return CascadeShadowMap0.SampleCmpLevelZero(ShadowSampler, uv, comparison);
        case 1: return CascadeShadowMap1.SampleCmpLevelZero(ShadowSampler, uv, comparison);
        case 2: return CascadeShadowMap2.SampleCmpLevelZero(ShadowSampler, uv, comparison);
        case 3: return CascadeShadowMap3.SampleCmpLevelZero(ShadowSampler, uv, comparison);
        default: return 1.0f; // No shadow
    }
}

// Sample cascade shadow map with regular point sampling (for blocker search)
float SampleCascadeShadowMapDepth(int cascadeIndex, float2 uv)
{
    switch (cascadeIndex)
    {
        case 0: return CascadeShadowMap0.SampleLevel(PointSampler, uv, 0).r;
        case 1: return CascadeShadowMap1.SampleLevel(PointSampler, uv, 0).r;
        case 2: return CascadeShadowMap2.SampleLevel(PointSampler, uv, 0).r;
        case 3: return CascadeShadowMap3.SampleLevel(PointSampler, uv, 0).r;
        default: return 1.0f;
    }
}

// CSM shadow sampling with PCSS
float SampleCSMShadow(float3 worldPos, float3 normal, float normalBias)
{
    // Transform world position to view space to get depth
    float4 viewPos = mul(float4(worldPos, 1.0f), matView);
    float viewDepth = -viewPos.z; // Negative because we're looking down -Z
    
    // Determine which cascade to use
    int cascadeIndex = GetCascadeIndex(viewDepth);
    if (cascadeIndex < 0 || cascadeIndex >= (int)CSMCascadeCount)
    {
        return 1.0f; // No shadow beyond cascades
    }
    
    // Apply normal bias
    float3 biasedWorldPos = worldPos + normal * (normalBias * CSMNormalBiasScale);
    
    // Transform to light space using the appropriate cascade matrix
    float4 lightSpacePos = mul(float4(biasedWorldPos, 1.0f), CascadeViewProj[cascadeIndex]);
    
    if (lightSpacePos.w <= 0.0f)
        return 1.0f;
        
    // Perspective division
    float3 ndc = lightSpacePos.xyz / lightSpacePos.w;
    
    // Convert to texture coordinates
    float2 uv = ndc.xy * float2(1.0f, -1.0f) * 0.5f + 0.5f;
    float receiverDepth = ndc.z;
    
    // Early out if outside shadow map
    if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f || ndc.z > 1.0f)
        return 1.0f;
    
    // Simple PCF for CSM (can be enhanced with PCSS later)
    float shadow = 0.0f;
    float2 texelSize = 1.0f / 2048.0f; // Cascade resolution
    int pcfSamples = 5;
    
    for (int x = -pcfSamples/2; x <= pcfSamples/2; ++x)
    {
        for (int y = -pcfSamples/2; y <= pcfSamples/2; ++y)
        {
            float2 offset = float2(x, y) * texelSize;
            float comparison = receiverDepth - CSMShadowBias;
            shadow += SampleCascadeShadowMap(cascadeIndex, uv + offset, comparison);
        }
    }
    
    return shadow / float(pcfSamples * pcfSamples);
}

float4 main(PS_INPUT input) : SV_TARGET
{
    // Sample G-Buffer textures
    float4 albedoData = AlbedoTexture.Sample(PointSampler, input.TexCoord);
    float4 normalData = NormalTexture.Sample(PointSampler, input.TexCoord);
    float4 materialData = MaterialTexture.Sample(PointSampler, input.TexCoord);
    float4 emissiveData = EmissiveTexture.Sample(PointSampler, input.TexCoord);
    float4 viewRayData = ViewRayTexture.Sample(PointSampler, input.TexCoord);
    float depth = normalData.a;
    
    float3 viewRay = normalize(viewRayData.xyz);
    
    // Extract G-Buffer data
    float3 albedo = albedoData.rgb;
    float opacity = albedoData.a;
    
    // Transform normal from [0, 1] to [-1, 1]
    float3 normal = normalData.rgb * 2.0f - 1.0f;
    
    float metallic = materialData.r;
    float roughness = materialData.g;
    float specular = materialData.b;
    float ao = materialData.a;
    
    float3 emissive = emissiveData.rgb;
    
    // Reconstruct world position from depth
    float3 worldPos = ReconstructPosition(depth, viewRay);

    // Initialize lighting with ambient and emissive
    float3 lighting = albedo * AmbientColor * ao + emissive;
    
    float3 viewDir = normalize(CameraPosition - worldPos);
    
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
            if (light.ShadowMap > 0)
            {
                // Check if CSM is enabled (CSMCascadeCount > 0)
                if (CSMCascadeCount > 0)
                {
                    shadow = SampleCSMShadow(worldPos, normal, 0.005f);
                }
            }
            
            lighting += CalculateDirectionalLight(light, viewDir, worldPos, normal, albedo, metallic, roughness, specular, shadow);
        }
        else if (light.Type == 2) // Spot light
        {
            lighting += CalculateSpotLight(light, viewDir, worldPos, normal, albedo, metallic, roughness, specular);
        }   
    }
      // Apply fog
    float distanceToCamera = length(worldPos - CameraPosition);
	float fogFactor = saturate((distanceToCamera - FogStart) / (FogEnd - FogStart));
    lighting = lerp(lighting, FogColor, fogFactor);

    // Apply ACES tone mapping
    lighting = ACESFilm(lighting);
    
    // Apply dithering before gamma correction to break up color banding in dark areas
    //lighting = ApplyDithering(lighting, input.Position.xy);
      // Apply gamma correction
    lighting = pow(abs(lighting), 1.0 / 2.2);

    return float4(lighting, opacity);
}
