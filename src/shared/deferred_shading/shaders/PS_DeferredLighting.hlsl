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

// Advanced multi-scale blocker search for precise contact hardening
float FindBlockerDepth(float2 uv, float receiverDepth, float searchRadius, out float numBlockers, float adaptiveBias)
{
    float blockerSum = 0.0f;
    numBlockers = 0.0f;
    
    // Multi-scale search: combine coarse and fine sampling
    // This provides better detection of blockers at various distances
    
    // Coarse search first (larger area, fewer samples)
    uint rotIndex = uint(fmod(dot(uv, float2(456.7, 234.8)), 16.0));
    float2x2 searchRotations[16] = {
        float2x2(1.0, 0.0, 0.0, 1.0),                      // 0°
        float2x2(0.965926, -0.258819, 0.258819, 0.965926), // 15°
        float2x2(0.866025, -0.5, 0.5, 0.866025),           // 30°
        float2x2(0.707107, -0.707107, 0.707107, 0.707107), // 45°
        float2x2(0.5, -0.866025, 0.866025, 0.5),           // 60°
        float2x2(0.258819, -0.965926, 0.965926, 0.258819), // 75°
        float2x2(0.0, -1.0, 1.0, 0.0),                     // 90°
        float2x2(-0.258819, -0.965926, 0.965926, -0.258819), // 105°
        float2x2(-0.5, -0.866025, 0.866025, -0.5),         // 120°
        float2x2(-0.707107, -0.707107, 0.707107, -0.707107), // 135°
        float2x2(-0.866025, -0.5, 0.5, -0.866025),         // 150°
        float2x2(-0.965926, -0.258819, 0.258819, -0.965926), // 165°
        float2x2(-1.0, 0.0, 0.0, -1.0),                    // 180°
        float2x2(-0.965926, 0.258819, -0.258819, -0.965926), // 195°
        float2x2(-0.866025, 0.5, -0.5, -0.866025),         // 210°
        float2x2(-0.707107, 0.707107, -0.707107, -0.707107)  // 225°
    };
    float2x2 rot = searchRotations[rotIndex];
    
    // Primary blocker search with weighted sampling
    float weightedBlockerSum = 0.0f;
    float totalWeight = 0.0f;
    for (int i = 0; i < 20; i++) // Increased for better accuracy
    {
        float2 offset = mul(rot, POISSON_DISK[i % 32]) * searchRadius;
        float shadowMapDepth = ShadowMap.SampleLevel(PointSampler, uv + offset, 0).r;
        
        // More precise blocker detection with depth gradient consideration
        float depthThreshold = 0.00002f; // Tighter threshold for precision
        if (shadowMapDepth < receiverDepth - depthThreshold)
        {
            // Weight blockers based on distance - closer blockers have more influence
            float weight = 1.0f - length(offset) / searchRadius;
            weight = weight * weight; // Quadratic weighting
            
            weightedBlockerSum += shadowMapDepth * weight;
            totalWeight += weight;
            numBlockers++;
        }
    }
    
    // Fine search near detected blockers (if any found)
    if (numBlockers > 0.0f)
    {
        float avgBlockerDepth = weightedBlockerSum / max(totalWeight, 0.001f);
        float2x2 fineRot = searchRotations[(rotIndex + 8) % 16];
        float fineRadius = searchRadius * 0.4f;
        
        float fineBlockerSum = 0.0f;
        float fineBlockerCount = 0.0f;
        
        for (int j = 0; j < 12; j++)
        {
            float2 offset = mul(fineRot, POISSON_DISK[j]) * fineRadius;
            float shadowMapDepth = ShadowMap.SampleLevel(PointSampler, uv + offset, 0).r;
            
            if (shadowMapDepth < receiverDepth - 0.00001f)
            {
                fineBlockerSum += shadowMapDepth;
                fineBlockerCount++;
            }
        }
        
        // Blend coarse and fine results for optimal precision
        if (fineBlockerCount > 0.0f)
        {
            float fineAvg = fineBlockerSum / fineBlockerCount;
            float blendFactor = fineBlockerCount / 12.0f; // More fine samples = more weight
            return lerp(avgBlockerDepth, fineAvg, blendFactor * 0.6f);
        }
        
        return avgBlockerDepth;
    }
    
    return receiverDepth; // No blockers found
}

// Production-Quality Anti-Aliased Shadow Filtering using Hybrid VSM + Advanced PCF
float PCF_Filter(float2 uv, float receiverDepth, float filterRadius, float adaptiveBias)
{
    // Use the actual high-resolution shadow map
    float2 texelSize = 1.0f / 8192.0f; // Match increased resolution
    
    // STEP 1: Adaptive super-sampling based on screen-space derivatives
    // This detects when we need more samples to hide texel structure
    float2 uvDDX = ddx(uv);
    float2 uvDDY = ddy(uv);
    float screenSpaceDerivative = length(uvDDX) + length(uvDDY);
    
    // Increase sample count when magnification would reveal texel structure
    int baseSamples = 24;
    int maxSamples = 64;
    float magnificationFactor = saturate(screenSpaceDerivative * 1000.0f);
    int sampleCount = int(lerp(float(baseSamples), float(maxSamples), magnificationFactor));
    
    // STEP 2: Multi-frequency rotation matrices to break up regular patterns
    float2x2 rotations[8] =
    {
        float2x2(1.0, 0.0, 0.0, 1.0), // 0°
        float2x2(0.707107, -0.707107, 0.707107, 0.707107), // 45°
        float2x2(0.0, -1.0, 1.0, 0.0), // 90°
        float2x2(-0.707107, -0.707107, 0.707107, -0.707107), // 135°
        float2x2(-1.0, 0.0, 0.0, -1.0), // 180°
        float2x2(-0.707107, 0.707107, -0.707107, -0.707107), // 225°
        float2x2(0.0, 1.0, -1.0, 0.0), // 270°
        float2x2(0.707107, 0.707107, -0.707107, 0.707107) // 315°
    };
    
    // Use screen-space position for stable rotation selection
    uint rotIndex = uint(fmod(dot(uv, float2(127.1, 311.7)) * 43758.5453, 8.0));
    float2x2 rot = rotations[rotIndex];
    
    // STEP 3: Multi-scale filtering to eliminate mosaic patterns
    float totalShadow = 0.0f;
    float totalWeight = 0.0f;
    
    // Primary filtering pass with larger radius
    float primaryRadius = filterRadius;
    for (int i = 0; i < sampleCount; i++)
    {
        float2 offset = mul(rot, POISSON_DISK[i % 32]) * primaryRadius;
        
        // Add slight sub-texel jitter to break up regular sampling
        float2 jitter = (frac(sin(dot(uv + offset, float2(12.9898, 78.233))) * 43758.5453) - 0.5) * texelSize * 0.5f;
        offset += jitter;
        
        float comparison = receiverDepth - adaptiveBias;
        float shadowValue = ShadowMap.SampleCmpLevelZero(ShadowSampler, uv + offset, comparison);
        
        // Distance-based weighting for smooth transitions
        float weight = 1.0f - saturate(length(offset) / primaryRadius);
        weight = weight * weight; // Quadratic falloff
        
        totalShadow += shadowValue * weight;
        totalWeight += weight;
    }
    
    float primaryResult = totalShadow / max(totalWeight, 0.001f);
    
    // STEP 4: Secondary micro-filtering pass to eliminate remaining artifacts
    float microShadow = 0.0f;
    float microWeight = 0.0f;
    float microRadius = filterRadius * 0.3f;
    
    // Use different rotation for micro pass
    float2x2 microRot = rotations[(rotIndex + 4) % 8];
    
    for (int j = 0; j < 16; j++)
    {
        float2 offset = mul(microRot, POISSON_DISK[j]) * microRadius;
        
        // More aggressive sub-texel jittering for micro pass
        float2 microJitter = (frac(sin(dot(uv + offset, float2(93.9, 67.1))) * 47583.2947) - 0.5) * texelSize;
        offset += microJitter;
        
        float comparison = receiverDepth - adaptiveBias * 0.9f;
        float shadowValue = ShadowMap.SampleCmpLevelZero(ShadowSampler, uv + offset, comparison);
        
        microShadow += shadowValue;
        microWeight += 1.0f;
    }
    
    float microResult = microShadow / microWeight;
    
    // STEP 5: Blend results based on magnification to eliminate mosaic
    float blendFactor = saturate(magnificationFactor * 0.6f);
    return lerp(primaryResult, microResult, blendFactor);
}

// Production-Quality PCSS with Contact Hardening - STABLE VERSION
float SampleShadow(float3 worldPos, float3 normal, float normalBias)
{
    // Calculate light direction
    float3 lightDir = -normalize(Lights[0].Direction);
    
    // Use simpler, more stable bias calculation
    float NdotL = saturate(dot(normal, lightDir));
    
    // Early out for back-facing surfaces
    if (NdotL < 0.05f)
        return 0.0f;
    
    // Conservative bias - prioritize stability over eliminating all acne
    float baseBias = ShadowBias;
    float slopeBias = (1.0f - NdotL) * 0.001f;
    float totalBias = baseBias + slopeBias;
    
    // Apply minimal normal bias to prevent peter panning
    float adjustedNormalBias = normalBias * NormalBiasScale * 0.5f; // Reduced by half
    adjustedNormalBias = min(adjustedNormalBias, 0.002f); // Very conservative limit
    
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
    
    // STEP 1: Blocker Search for Contact Hardening
    float searchWidth = LightSize * 0.5f; // Conservative search radius
    float numBlockers = 0.0f;
    float avgBlockerDepth = FindBlockerDepth(uv, receiverDepth, searchWidth, numBlockers, totalBias);
    
    // If no blockers found, no shadow
    if (numBlockers < 1.0f)
        return 1.0f;
    
    // STEP 2: Contact Hardening - calculate penumbra size
    // Smaller penumbra for contact shadows, larger for distant shadows
    float penumbraSize = (receiverDepth - avgBlockerDepth) / avgBlockerDepth;
    penumbraSize = clamp(penumbraSize * LightSize * 2.0f, 0.001f, 0.004f); // Tighter limits
    
    // STEP 3: High-Quality PCF with adaptive radius
    return PCF_Filter(uv, receiverDepth, penumbraSize, totalBias);
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
        }        else if (light.Type == 1) // Directional light
        {
            float shadow = 1.0f;
            if (light.ShadowMap > 0)
            {
                // Use very conservative normal bias to prevent peter panning and noise
                shadow = SampleShadow(worldPos, normal, 0.005f);
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
