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

Texture2D ShadowMap : register(t5);

// Samplers
SamplerState PointSampler : register(s0);
SamplerComparisonState ShadowSampler : register(s1);

// Poisson disk samples for shadow sampling
static const float2 POISSON_DISK[16] = {
    float2(-0.94201624, -0.39906216),
    float2(0.94558609, -0.76890725),
    float2(-0.094184101, -0.92938870),
    float2(0.34495938, 0.29387760),
    float2(-0.91588581, 0.45771432),
    float2(-0.81544232, -0.87912464),
    float2(-0.38277543, 0.27676845),
    float2(0.97484398, 0.75648379),
    float2(0.44323325, -0.97511554),
    float2(0.53742981, -0.47373420),
    float2(-0.26496911, -0.41893023),
    float2(0.79197514, 0.19090188),
    float2(-0.24188840, 0.99706507),
    float2(-0.81409955, 0.91437590),
    float2(0.19984126, 0.78641367),
    float2(0.14383161, -0.14100790)
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

// Helper function to find average blocker depth
float FindBlockerDepth(float2 uv, float receiverDepth, float searchRadius, out float numBlockers)
{
    float blockerSum = 0.0f;
    numBlockers = 0.0f;
    
    // Generate a random rotation angle to reduce banding artifacts
    float angle = frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453) * 3.14159;
    float2x2 rot = float2x2(cos(angle), -sin(angle), sin(angle), cos(angle));
    
    // Sample potential blockers
    for (int i = 0; i < 16; i++)
    {
        float2 offset = mul(rot, POISSON_DISK[i]) * searchRadius;
        float shadowMapDepth = ShadowMap.SampleLevel(PointSampler, uv + offset, 0).r;
        
        // If the depth is less than the receiver's depth, it's a blocker
        if (shadowMapDepth < receiverDepth - ShadowBias)
        {
            blockerSum += shadowMapDepth;
            numBlockers++;
        }
    }
    
    if (numBlockers > 0.0f)
        return blockerSum / numBlockers;
        
    return 1.0f;
}

// Percentage-Closer Filtering with variable filter size
float PCF_Filter(float2 uv, float receiverDepth, float filterRadius)
{
    float sum = 0.0f;
    
    // Generate a random rotation angle to reduce banding artifacts
    float angle = frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453) * 3.14159;
    float2x2 rot = float2x2(cos(angle), -sin(angle), sin(angle), cos(angle));
    
    // Higher sample count for larger filter radius
    int sampleCount = lerp(16, 32, saturate(filterRadius * 10.0));
    sampleCount = min(sampleCount, 16); // Cap at 16 samples for performance
    
    for (int i = 0; i < sampleCount; i++)
    {
        float2 offset = mul(rot, POISSON_DISK[i % 16]) * filterRadius;
        sum += ShadowMap.SampleCmpLevelZero(ShadowSampler, uv + offset, receiverDepth - ShadowBias);
    }
    
    return sum / float(sampleCount);
}

// Advanced shadow sampling using PCSS (Percentage Closer Soft Shadows)
float SampleShadow(float3 worldPos, float3 normal, float normalBias)
{
    // Apply normal-based bias to reduce shadow acne on sloped surfaces
    float adjustedNormalBias = normalBias * NormalBiasScale;
    float3 biasedWorldPos = worldPos + normal * adjustedNormalBias;
    
    // Transform to light space
    float4 lightSpacePos = mul(float4(biasedWorldPos, 1.0f), LightViewProj);
    
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
    
    // Check if surface is facing light
    float NdotL = dot(normal, -normalize(Lights[0].Direction));
    if (NdotL <= 0.0f)
        return 0.0f;  // Surface faces away from light
    
    // ------------------------
    // STEP 1: Blocker search
    // ------------------------
    float numBlockers;
    float searchRadius = BlockerSearchRadius / lightSpacePos.w; // Scale with depth
    float avgBlockerDepth = FindBlockerDepth(uv, receiverDepth, searchRadius, numBlockers);
    
    // Early out if no blockers found
    if (numBlockers < 1.0f)
        return 1.0f;
    
    // ------------------------
    // STEP 2: Penumbra estimation
    // ------------------------
    // Estimate penumbra size based on blocker distance
    float penumbraRatio = (receiverDepth - avgBlockerDepth) / avgBlockerDepth;
    float filterRadius = penumbraRatio * LightSize * ShadowSoftness;
    
    // Adjust filter radius based on distance to avoid excessive blurring for distant objects
    filterRadius = min(filterRadius, 0.01f);
    
    // Ensure minimum filter size to prevent hard shadow edges
    filterRadius = max(filterRadius, 1.0f / 4096.0f);
    
    // ------------------------
    // STEP 3: Filtering
    // ------------------------
    return PCF_Filter(uv, receiverDepth, filterRadius);
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

    float3 specularBRDF = (D * G * F) / (4.0 * max(NdotV * NdotL, 0.001));
    specularBRDF *= specular;

    float3 kS = F;
    float3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;

    float3 diffuse = albedo / PI;

    float3 radiance = light.Color * light.Intensity;

    return ((kD * diffuse + specularBRDF) * radiance * NdotL) * shadow;
}

// Calculates spot light contribution
float3 CalculateSpotLight(Light light, float3 viewDir, float3 worldPos, float3 normal, float3 albedo, float metallic, float roughness, float specular)
{
    float3 lightDir = light.Position - worldPos;
    float distance = length(lightDir);

    if (distance > light.Range)
        return float3(0, 0, 0);

    lightDir = normalize(lightDir);

    // Spot cone
    float spotFactor = dot(lightDir, -normalize(light.Direction));
    float spotCutoff = cos(radians(light.SpotAngle * 0.5));
    if (spotFactor < spotCutoff)
        return float3(0, 0, 0);

    float spotAttenuation = smoothstep(spotCutoff, spotCutoff + 0.1, spotFactor);

    // Distance attenuation
    float attenuation = pow(1.0 - saturate(distance / light.Range), 2.0) * spotAttenuation;

    // Cook-Torrance lighting
    float3 halfway = normalize(lightDir + viewDir);

    float NdotL = max(dot(normal, lightDir), 0.0);
    float NdotV = max(dot(normal, viewDir), 0.0);
    float NdotH = max(dot(normal, halfway), 0.0);
    float VdotH = max(dot(viewDir, halfway), 0.0);

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float3 F = F_Schlick(F0, VdotH);
    float D = D_GGX(NdotH, roughness);
    float G = G_Smith(NdotV, NdotL, roughness);

    float3 specularBRDF = (D * G * F) / (4.0 * max(NdotV * NdotL, 0.001));
    specularBRDF *= specular;

    float3 kS = F;
    float3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;

    float3 diffuse = albedo / PI;
    float3 radiance = light.Color * light.Intensity * attenuation;

    return (kD * diffuse + specularBRDF) * radiance * NdotL;
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
                shadow = SampleShadow(worldPos, normal, 0.078125f * 0.2f);
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
    lighting = ApplyDithering(lighting, input.Position.xy);
    
    // Apply gamma correction
    lighting = pow(lighting, 1.0 / 2.2);

    return float4(lighting, opacity);
}
