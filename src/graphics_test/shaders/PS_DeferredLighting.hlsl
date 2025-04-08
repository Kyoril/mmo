// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Input structure from the vertex shader
struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
    float2 TexCoord : TEXCOORD0;
    float3 ViewRay : TEXCOORD1;
};

// G-Buffer textures
Texture2D AlbedoTexture : register(t0);
Texture2D NormalTexture : register(t1);
Texture2D MaterialTexture : register(t2);
Texture2D EmissiveTexture : register(t3);

// Samplers
SamplerState PointSampler : register(s0);

// Camera constant buffer
cbuffer CameraBuffer : register(b0)
{
    float3 CameraPosition;
    float FogStart;
    float FogEnd;
    float3 FogColor;
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
    float3 Padding;
};

// Light buffer
cbuffer LightBuffer : register(b1)
{
    uint LightCount;
    float3 AmbientColor;
    Light Lights[16];
}

// Reconstructs world position from depth
float3 ReconstructPosition(float2 texCoord, float depth, float3 viewRay)
{
    // Convert from [0,1] to [-1,1] for clip space
    float z = depth;
    
    // Scale the view ray by the depth to get the world position
    return CameraPosition + viewRay * z;
}

// Calculates point light contribution
float3 CalculatePointLight(Light light, float3 worldPos, float3 normal, float3 albedo, float metallic, float roughness, float specular)
{
    float3 lightDir = light.Position - worldPos;
    float distance = length(lightDir);
    
    // Early out if we're outside the light range
    if (distance > light.Range)
        return float3(0, 0, 0);
    
    lightDir = normalize(lightDir);
    
    // Calculate attenuation
    float attenuation = 1.0 - saturate(distance / light.Range);
    attenuation = attenuation * attenuation;
    
    // Calculate diffuse lighting
    float NdotL = max(dot(normal, lightDir), 0.0);
    float3 diffuse = albedo * NdotL;
    
    // Calculate view direction and half vector for specular
    float3 viewDir = normalize(CameraPosition - worldPos);
    float3 halfVector = normalize(lightDir + viewDir);
    
    // Calculate specular lighting (simplified Blinn-Phong)
    float NdotH = max(dot(normal, halfVector), 0.0);
    float specPower = (1.0 - roughness) * 100.0 + 8.0;
    float specularIntensity = pow(NdotH, specPower) * specular;
    
    // Mix between diffuse and specular based on metallic
    float3 specularColor = lerp(float3(1, 1, 1), albedo, metallic);
    float3 result = lerp(diffuse, specularColor * specularIntensity, metallic);
    
    // Apply light color and attenuation
    return result * light.Color * light.Intensity * attenuation;
}

// Calculates directional light contribution
float3 CalculateDirectionalLight(Light light, float3 normal, float3 albedo, float metallic, float roughness, float specular, float3 worldPos)
{
    float3 lightDir = -normalize(light.Direction);
    
    // Calculate diffuse lighting
    float NdotL = max(dot(normal, lightDir), 0.0);
    float3 diffuse = albedo * NdotL;
    
    // Calculate view direction and half vector for specular
    float3 viewDir = normalize(CameraPosition - worldPos);
    float3 halfVector = normalize(lightDir + viewDir);
    
    // Calculate specular lighting (simplified Blinn-Phong)
    float NdotH = max(dot(normal, halfVector), 0.0);
    float specPower = (1.0 - roughness) * 100.0 + 8.0;
    float specularIntensity = pow(NdotH, specPower) * specular;
    
    // Mix between diffuse and specular based on metallic
    float3 specularColor = lerp(float3(1, 1, 1), albedo, metallic);
    float3 result = lerp(diffuse, specularColor * specularIntensity, metallic);
    
    // Apply light color
    return result * light.Color * light.Intensity;
}

// Calculates spot light contribution
float3 CalculateSpotLight(Light light, float3 worldPos, float3 normal, float3 albedo, float metallic, float roughness, float specular)
{
    float3 lightDir = light.Position - worldPos;
    float distance = length(lightDir);
    
    // Early out if we're outside the light range
    if (distance > light.Range)
        return float3(0, 0, 0);
    
    lightDir = normalize(lightDir);
    
    // Calculate spot cone attenuation
    float spotFactor = dot(lightDir, -normalize(light.Direction));
    float spotCutoff = cos(radians(light.SpotAngle * 0.5));
    
    // Early out if outside the spot cone
    if (spotFactor < spotCutoff)
        return float3(0, 0, 0);
    
    // Smooth the spot edge
    float spotAttenuation = smoothstep(spotCutoff, spotCutoff + 0.1, spotFactor);
    
    // Calculate distance attenuation
    float attenuation = 1.0 - saturate(distance / light.Range);
    attenuation = attenuation * attenuation * spotAttenuation;
    
    // Calculate diffuse lighting
    float NdotL = max(dot(normal, lightDir), 0.0);
    float3 diffuse = albedo * NdotL;
    
    // Calculate view direction and half vector for specular
    float3 viewDir = normalize(CameraPosition - worldPos);
    float3 halfVector = normalize(lightDir + viewDir);
    
    // Calculate specular lighting (simplified Blinn-Phong)
    float NdotH = max(dot(normal, halfVector), 0.0);
    float specPower = (1.0 - roughness) * 100.0 + 8.0;
    float specularIntensity = pow(NdotH, specPower) * specular;
    
    // Mix between diffuse and specular based on metallic
    float3 specularColor = lerp(float3(1, 1, 1), albedo, metallic);
    float3 result = lerp(diffuse, specularColor * specularIntensity, metallic);
    
    // Apply light color and attenuation
    return result * light.Color * light.Intensity * attenuation;
}

float4 main(PS_INPUT input) : SV_TARGET
{
    // Sample G-Buffer textures
    float4 albedoData = AlbedoTexture.Sample(PointSampler, input.TexCoord);
    float4 normalData = NormalTexture.Sample(PointSampler, input.TexCoord);
    float4 materialData = MaterialTexture.Sample(PointSampler, input.TexCoord);
    float4 emissiveData = EmissiveTexture.Sample(PointSampler, input.TexCoord);
    float depth = normalData.a;
    
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
    float3 worldPos = ReconstructPosition(input.TexCoord, depth, input.ViewRay);
    
    // Initialize lighting with ambient and emissive
    float3 lighting = albedo * AmbientColor * ao + emissive;
    
    // Calculate lighting for each light
    for (uint i = 0; i < LightCount; i++)
    {
        Light light = Lights[i];
        
        if (light.Type == 0) // Point light
        {
            lighting += CalculatePointLight(light, worldPos, normal, albedo, metallic, roughness, specular);
        }
        else if (light.Type == 1) // Directional light
        {
            lighting += CalculateDirectionalLight(light, normal, albedo, metallic, roughness, specular, worldPos);
        }
        else if (light.Type == 2) // Spot light
        {
            lighting += CalculateSpotLight(light, worldPos, normal, albedo, metallic, roughness, specular);
        }
    }
    
    // Apply fog
    float distanceToCamera = length(worldPos - CameraPosition);
    float fogFactor = saturate((distanceToCamera - FogStart) / (FogEnd - FogStart));
    lighting = lerp(lighting, FogColor, fogFactor);

    lighting = normalData.a;

    return float4(lighting, opacity);
}
