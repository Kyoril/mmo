// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

static const float PI = 3.14159265359;
static const uint NUM_SHADOW_CASCADES = 4;

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
Texture2D ShadowMapCascade0 : register(t5);
Texture2D ShadowMapCascade1 : register(t6);
Texture2D ShadowMapCascade2 : register(t7);
Texture2D ShadowMapCascade3 : register(t8);

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
    // Cascade view-projection matrices
    column_major matrix CascadeViewProj[NUM_SHADOW_CASCADES];
    
    // Cascade split distances (in view space)
    float4 CascadeSplitDistances;
    
    // Shadow filter parameters
    float ShadowBias;           // Depth bias to prevent shadow acne
    float NormalBiasScale;      // Bias scale factor based on normal
    float ShadowSoftness;       // Controls general softness of shadows
    float BlockerSearchRadius;  // Search radius for the blocker search phase
    
    float LightSize;            // Controls the size of the virtual light (larger = softer shadows)
    uint CascadeCount;          // Number of active cascades
    uint DebugCascades;         // Whether to show cascade debug colors
    float CascadeBlendFactor;   // Blend factor for cascade transitions
};

// Debug colors for cascade visualization
static const float3 CASCADE_COLORS[4] = {
    float3(1.0, 0.0, 0.0),  // Red - cascade 0 (nearest)
    float3(0.0, 1.0, 0.0),  // Green - cascade 1
    float3(0.0, 0.0, 1.0),  // Blue - cascade 2
    float3(1.0, 1.0, 0.0)   // Yellow - cascade 3 (farthest)
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

// Helper function to find average blocker depth for a specific cascade
float FindBlockerDepthCascade(Texture2D shadowMap, float2 uv, float receiverDepth, float searchRadius, out float numBlockers)
{
    float blockerSum = 0.0f;
    numBlockers = 0.0f;
    
    // Generate a random rotation angle to reduce banding artifacts
    float angle = frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453) * PI;
    float cosAngle = cos(angle);
    float sinAngle = sin(angle);
    float2x2 rot = float2x2(cosAngle, -sinAngle, sinAngle, cosAngle);
    
    // Sample potential blockers
    [unroll]
    for (int i = 0; i < 16; i++)
    {
        float2 offset = mul(rot, POISSON_DISK[i]) * searchRadius;
        float shadowMapDepth = shadowMap.SampleLevel(PointSampler, uv + offset, 0).r;
        
        // If the depth is less than the receiver's depth, it's a blocker
        if (shadowMapDepth < receiverDepth - ShadowBias)
        {
            blockerSum += shadowMapDepth;
            numBlockers++;
        }
    }
    
    if (numBlockers > 0.0f)
    {
        return blockerSum / numBlockers;
    }
        
    return 1.0f;
}

// Optimized PCF filter with rotated Poisson disk
float PCF_FilterCascade(Texture2D shadowMap, float2 uv, float receiverDepth, float filterRadius)
{
    float sum = 0.0f;
    
    // Generate a random rotation angle to reduce banding artifacts
    float angle = frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453) * PI;
    float cosAngle = cos(angle);
    float sinAngle = sin(angle);
    float2x2 rot = float2x2(cosAngle, -sinAngle, sinAngle, cosAngle);
    
    // Use fixed 16 samples for consistent quality
    [unroll]
    for (int i = 0; i < 16; i++)
    {
        float2 offset = mul(rot, POISSON_DISK[i]) * filterRadius;
        sum += shadowMap.SampleCmpLevelZero(ShadowSampler, uv + offset, receiverDepth - ShadowBias);
    }
    
    return sum / 16.0f;
}

// Sample shadow from a specific cascade
float SampleShadowCascade(Texture2D shadowMap, float3 worldPos, float3 normal, uint cascadeIndex)
{
    // Apply normal-based bias to reduce shadow acne on sloped surfaces
    float3 lightDir = -normalize(Lights[0].Direction);
    float NdotL = saturate(dot(normal, lightDir));
    
    // Scale bias based on slope - more bias for grazing angles
    float slopeBias = NormalBiasScale * (1.0 - NdotL);
    float3 biasedWorldPos = worldPos + normal * slopeBias;
    
    // Transform to light space
    float4 lightSpacePos = mul(float4(biasedWorldPos, 1.0f), CascadeViewProj[cascadeIndex]);
    
    if (lightSpacePos.w <= 0.0f)
    {
        return 1.0f;
    }
        
    // Perspective division
    float3 ndc = lightSpacePos.xyz / lightSpacePos.w;
    
    // Convert to texture coordinates
    float2 uv = ndc.xy * float2(1.0f, -1.0f) * 0.5f + 0.5f;
    float receiverDepth = ndc.z;
    
    // Early out if outside shadow map
    if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f || ndc.z > 1.0f || ndc.z < 0.0f)
    {
        return 1.0f;
    }
    
    // Check if surface is facing light
    if (NdotL <= 0.0f)
    {
        return 0.0f;  // Surface faces away from light
    }
    
    // Calculate filter radius based on cascade (larger cascades need larger filter radius in UV space)
    // But since each cascade has similar world-space coverage per texel, use consistent filtering
    float baseFilterRadius = 1.0f / 2048.0f;  // Base texel size
    float filterRadius = baseFilterRadius * (1.0f + float(cascadeIndex) * 0.5f) * ShadowSoftness;
    
    // Use simpler PCF for quality/performance balance
    return PCF_FilterCascade(shadowMap, uv, receiverDepth, filterRadius);
}

// Select cascade and sample shadow with optional blending
float SampleShadowCSM(float3 worldPos, float3 normal, float viewDepth, out uint selectedCascade)
{
    selectedCascade = 0;
    
    // Early out if no cascades
    if (CascadeCount == 0)
    {
        return 1.0f;
    }
    
    // Find the appropriate cascade based on view depth
    // Cascades are ordered by distance, so find the first one that contains our depth
    float cascadeSplits[4] = { 
        CascadeSplitDistances.x, 
        CascadeSplitDistances.y, 
        CascadeSplitDistances.z, 
        CascadeSplitDistances.w 
    };
    
    // Select cascade - find the first cascade where our depth is within range
    selectedCascade = CascadeCount - 1;  // Default to last cascade
    for (uint i = 0; i < CascadeCount; i++)
    {
        if (viewDepth <= cascadeSplits[i])
        {
            selectedCascade = i;
            break;
        }
    }
    
    // Sample the selected cascade
    float shadow = 1.0f;
    
    // Use switch for proper texture sampling (can't index textures dynamically in SM5)
    switch (selectedCascade)
    {
        case 0: shadow = SampleShadowCascade(ShadowMapCascade0, worldPos, normal, 0); break;
        case 1: shadow = SampleShadowCascade(ShadowMapCascade1, worldPos, normal, 1); break;
        case 2: shadow = SampleShadowCascade(ShadowMapCascade2, worldPos, normal, 2); break;
        case 3: shadow = SampleShadowCascade(ShadowMapCascade3, worldPos, normal, 3); break;
    }
    
    // Optional: Blend between cascades for smoother transitions
    if (CascadeBlendFactor > 0.0f && selectedCascade < CascadeCount - 1)
    {
        float currentSplit = cascadeSplits[selectedCascade];
        float blendStart = currentSplit * (1.0f - CascadeBlendFactor);
        
        if (viewDepth > blendStart)
        {
            float blendWeight = (viewDepth - blendStart) / (currentSplit - blendStart);
            blendWeight = smoothstep(0.0f, 1.0f, blendWeight);
            
            float nextShadow = 1.0f;
            switch (selectedCascade + 1)
            {
                case 1: nextShadow = SampleShadowCascade(ShadowMapCascade1, worldPos, normal, 1); break;
                case 2: nextShadow = SampleShadowCascade(ShadowMapCascade2, worldPos, normal, 2); break;
                case 3: nextShadow = SampleShadowCascade(ShadowMapCascade3, worldPos, normal, 3); break;
            }
            
            shadow = lerp(shadow, nextShadow, blendWeight);
        }
    }
    
    return shadow;
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

    // Calculate view depth for cascade selection - use distance from camera
    float viewDepth = length(worldPos - CameraPosition);

    // Initialize lighting with ambient and emissive
    float3 lighting = albedo * AmbientColor * ao + emissive;
    
    float3 viewDir = normalize(CameraPosition - worldPos);
    
    // Debug cascade visualization color
    float3 cascadeDebugColor = float3(1.0, 1.0, 1.0);
    
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
            if (light.ShadowMap > 0 && CascadeCount > 0)
            {
                uint selectedCascade = 0;
                shadow = SampleShadowCSM(worldPos, normal, viewDepth, selectedCascade);
                
                // Store cascade debug color
                if (DebugCascades > 0)
                {
                    cascadeDebugColor = CASCADE_COLORS[selectedCascade];
                }
            }
            
            lighting += CalculateDirectionalLight(light, viewDir, worldPos, normal, albedo, metallic, roughness, specular, shadow);
        }
        else if (light.Type == 2) // Spot light
        {
            lighting += CalculateSpotLight(light, viewDir, worldPos, normal, albedo, metallic, roughness, specular);
        }   
    }
    
    // Apply cascade debug visualization
    if (DebugCascades > 0)
    {
        lighting = lerp(lighting, lighting * cascadeDebugColor, 0.3);
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
