// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "deferred_renderer.h"
#include "cascaded_shadow_camera_setup.h"

#include "frame_ui/rect.h"
#include "graphics/graphics_device.h"
#include "graphics/shader_compiler.h"
#include "scene_graph/camera.h"
#include "scene_graph/render_queue.h"
#include "log/default_log_levels.h"

#ifdef WIN32
#   include <Windows.h>
#   include "shaders/VS_DeferredLighting.h"
#   include "shaders/PS_DeferredLighting.h"
#   include "graphics_d3d11/graphics_device_d3d11.h"
#endif

namespace mmo
{
    // Light structure that matches the one in the shader
    struct alignas(16) ShaderLight
    {
        Vector3 position;
        float range;
        Vector3 color;
        float intensity;
        Vector3 direction;
        float spotAngle;
        uint32 type;  // 0 = Point, 1 = Directional, 2 = Spot
        int32 shadowMap;
        Vector2 padding;
    };

	struct alignas(16) ShadowBuffer
    {
        // Cascade view-projection matrices
        Matrix4 cascadeViewProjections[NUM_SHADOW_CASCADES];
        
        // Cascade split distances (in view space)
        Vector4 cascadeSplitDistances;
        
        float shadowBias;           // Depth bias to prevent shadow acne
        float normalBiasScale;      // Bias scale factor based on normal
        float shadowSoftness;       // Controls general softness of shadows  
        float blockerSearchRadius;  // Search radius for the blocker search phase
        
        float lightSize;            // Controls the size of the virtual light (larger = softer shadows)
        uint32 cascadeCount;        // Number of active cascades
        uint32 debugCascades;       // Whether to show cascade debug colors
        float cascadeBlendFactor;   // Blend factor for cascade transitions
    };

    // Light buffer structure that matches the one in the shader
    struct alignas(16) LightBuffer
    {
        uint32 lightCount;
        Vector3 ambientColor;
        ShaderLight lights[DeferredRenderer::MAX_LIGHTS];
    };

	DeferredRenderer::DeferredRenderer(GraphicsDevice& device, Scene& scene, uint32 width, uint32 height)
        : m_device(device)
		, m_scene(scene)
        , m_gBuffer(device, width, height)
    {
        m_shadowCameraSetup = std::make_shared<DefaultShadowCameraSetup>();
        m_cascadedShadowSetup = std::make_shared<CascadedShadowCameraSetup>();
        
        // Configure CSM defaults for high quality
        auto& csmConfig = m_cascadedShadowSetup->GetConfig();
        csmConfig.maxShadowDistance = 300.0f;
        csmConfig.shadowMapSize = m_shadowMapSize;
        csmConfig.splitLambda = 0.92f;  // Mostly logarithmic for better near-camera detail
        csmConfig.stableCascades = true;
        csmConfig.cascadeBlendFactor = 0.1f;
        
#ifdef WIN32
		m_deferredLightVs = m_device.CreateShader(ShaderType::VertexShader, g_VS_DeferredLighting, std::size(g_VS_DeferredLighting));
        m_deferredLightPs = m_device.CreateShader(ShaderType::PixelShader, g_PS_DeferredLighting, std::size(g_PS_DeferredLighting));
#endif
        // Create the light buffer
        m_lightBuffer = m_device.CreateConstantBuffer(sizeof(LightBuffer), nullptr);
        m_shadowBuffer = m_device.CreateConstantBuffer(sizeof(ShadowBuffer), nullptr);

        // Create the render texture with 16-bit per channel format to reduce banding
        m_renderTexture = m_device.CreateRenderTexture("DeferredOutput", width, height, RenderTextureFlags::HasColorBuffer | RenderTextureFlags::ShaderResourceView, R16G16B16A16);
        ASSERT(m_renderTexture);

        const uint32 color = 0xFFFFFFFF;
        const POS_COL_TEX_VERTEX vertices[6]{
            { { -1.0f,	-1.0f,		0.0f }, color, { 0.0f, 1.0f } },
            { { -1.0f,	1.0f,		0.0f }, color, { 0.0f, 0.0f } },
            { { 1.0f,	1.0f,		0.0f }, color, { 1.0f, 0.0f } },
            { { 1.0f,	1.0f,		0.0f }, color, { 1.0f, 0.0f } },
            { { 1.0f,	-1.0f,		0.0f }, color, { 1.0f, 1.0f } },
            { { -1.0f,	-1.0f,		0.0f }, color, { 0.0f, 1.0f } }
        };

        m_quadBuffer = m_device.CreateVertexBuffer(6, sizeof(POS_COL_TEX_VERTEX), BufferUsage::StaticWriteOnly, vertices);
        
		// Create shadow maps for each cascade
        for (uint32 i = 0; i < NUM_SHADOW_CASCADES; ++i)
        {
            m_cascadeShadowMaps[i] = m_device.CreateRenderTexture(
                "ShadowMapCascade" + std::to_string(i), 
                m_shadowMapSize, 
                m_shadowMapSize, 
                RenderTextureFlags::HasDepthBuffer | RenderTextureFlags::ShaderResourceView);
        }
        
        // Keep a reference to the first cascade for legacy single-shadow-map code paths
		m_shadowMapRT = m_cascadeShadowMaps[0];

        // Setup shadow cameras for each cascade
        for (uint32 i = 0; i < NUM_SHADOW_CASCADES; ++i)
        {
            m_shadowCameraNodes[i] = m_scene.GetRootSceneNode().CreateChildSceneNode("__ShadowCameraNode_" + std::to_string(i) + "__");
            m_shadowCameras[i] = m_scene.CreateCamera("__DeferredShadowCamera_" + std::to_string(i) + "__");
            m_shadowCameraNodes[i]->AttachObject(*m_shadowCameras[i]);
        }

#ifdef WIN32        // TODO: Fix me: Move me to graphicsd3d11
        D3D11_SAMPLER_DESC sampDesc = {};
        // Using anisotropic filtering for better quality at oblique angles
        sampDesc.Filter = D3D11_FILTER_COMPARISON_ANISOTROPIC;
        // Use border addressing to avoid shadow edge artifacts
        sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
        sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
        sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
        sampDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;  // Shadow test: fragment depth < stored depth
        // White border color to avoid darkening at edges
        sampDesc.BorderColor[0] = 1.0f;
        sampDesc.BorderColor[1] = 1.0f;
        sampDesc.BorderColor[2] = 1.0f;
        sampDesc.BorderColor[3] = 1.0f;
        sampDesc.MinLOD = 0;
        sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
        // Increase anisotropy level for better quality
        sampDesc.MaxAnisotropy = 4;
        sampDesc.MipLODBias = 0;

		GraphicsDeviceD3D11& d3ddev = (GraphicsDeviceD3D11&)device;
        ID3D11Device& d3d11dev = d3ddev;
        d3d11dev.CreateSamplerState(&sampDesc, &m_shadowSampler);
#endif
    }

    DeferredRenderer::~DeferredRenderer()
    {
        for (uint32 i = 0; i < NUM_SHADOW_CASCADES; ++i)
        {
            if (m_shadowCameras[i])
            {
                m_scene.DestroyCamera(*m_shadowCameras[i]);
                m_shadowCameras[i] = nullptr;
            }

            if (m_shadowCameraNodes[i])
            {
                m_scene.DestroySceneNode(*m_shadowCameraNodes[i]);
                m_shadowCameraNodes[i] = nullptr;
            }
        }
    }

    void DeferredRenderer::Resize(uint32 width, uint32 height)
    {
        // Resize the G-Buffer
        m_gBuffer.Resize(width, height);
		m_renderTexture->Resize(width, height);
    }

    void DeferredRenderer::Render(Scene& scene, Camera& camera)
    {
        // Traverse the scene graph and find all kind of lighting information for the current frame
        FindLights(scene, camera);

        if (m_shadowCastingDirectionalLight)
        {
            if (m_useCascadedShadows)
            {
                // Render cascaded shadow maps
                RenderCascadedShadowMaps(scene, camera);
            }
            else
            {
                // Render the legacy single shadow map for the directional light
                RenderShadowMap(scene, camera);
            }
        }
        else
        {
            // No shadow-casting light - ensure shadow buffer indicates no shadows
            ShadowBuffer buffer{};
            buffer.cascadeCount = 0;
            m_shadowBuffer->Update(&buffer);
        }

        // Render the geometry pass
        RenderGeometryPass(scene, camera);

        // Render the lighting pass
        RenderLightingPass(scene, camera);
    }

    void DeferredRenderer::RenderGeometryPass(Scene& scene, Camera& camera)
    {
        // Bind the G-Buffer
        m_gBuffer.Bind();

        m_gBuffer.GetAlbedoRT().Clear(ClearFlags::Color);
        m_gBuffer.GetNormalRT().Clear(ClearFlags::Color);
        m_gBuffer.GetEmissiveRT().Clear(ClearFlags::Color);
        m_gBuffer.GetMaterialRT().Clear(ClearFlags::Color);
        m_gBuffer.GetViewRayRT().Clear(ClearFlags::Color);
        
        // Render the scene using the camera
        scene.Render(camera, PixelShaderType::GBuffer);
    }

    void DeferredRenderer::RenderLightingPass(Scene& scene, Camera& camera)
    {
        // Unbind the G-Buffer
        m_gBuffer.Unbind();

        // Set the render target to the back buffer
        m_renderTexture->Activate();

        // Disable depth testing and writing
        m_device.SetDepthEnabled(false);
        m_device.SetDepthWriteEnabled(false);

        m_device.SetTransformMatrix(World, Matrix4::Identity);
        m_device.SetTransformMatrix(View, camera.GetViewMatrix());
        m_device.SetTransformMatrix(Projection, camera.GetProjectionMatrix());
        scene.RefreshCameraBuffer(camera);

        m_gBuffer.GetAlbedoRT().Bind(ShaderType::PixelShader, 0);
        m_gBuffer.GetNormalRT().Bind(ShaderType::PixelShader, 1);
        m_gBuffer.GetMaterialRT().Bind(ShaderType::PixelShader, 2);
        m_gBuffer.GetEmissiveRT().Bind(ShaderType::PixelShader, 3);
        m_gBuffer.GetViewRayRT().Bind(ShaderType::PixelShader, 4);
        
        // Bind all cascade shadow maps
        for (uint32 i = 0; i < NUM_SHADOW_CASCADES; ++i)
        {
            m_cascadeShadowMaps[i]->Bind(ShaderType::PixelShader, 5 + i);
        }

        // Set the vertex format for the full-screen quad
        m_device.SetVertexFormat(VertexFormat::PosColorTex1);

        // Bind the G-Buffer textures to the lighting material
        m_deferredLightVs->Set();
        m_deferredLightPs->Set();

        // Set the topology type for the full-screen quad
        m_device.SetTopologyType(TopologyType::TriangleList);

        // Render a full screen quad
    	m_quadBuffer->Set(0);

        // Bind buffer to stage
        scene.GetCameraBuffer()->BindToStage(ShaderType::PixelShader, 1);
        m_lightBuffer->BindToStage(ShaderType::PixelShader, 2);
        m_shadowBuffer->BindToStage(ShaderType::PixelShader, 3);

        m_device.SetFillMode(FillMode::Solid);
        m_device.SetFaceCullMode(FaceCullMode::None);
        m_device.SetTextureAddressMode(TextureAddressMode::Clamp, TextureAddressMode::Clamp, TextureAddressMode::Clamp);
        m_device.SetTextureFilter(TextureFilter::Trilinear);

#ifdef WIN32
        GraphicsDeviceD3D11& d3ddev = (GraphicsDeviceD3D11&)GraphicsDevice::Get();
        ID3D11DeviceContext& d3d11ctx = d3ddev;
		ID3D11SamplerState* samplers[1] = { m_shadowSampler.Get() };
        d3d11ctx.PSSetSamplers(1, 1, samplers);
#endif
        
        // Draw a full-screen quad
        m_device.Draw(6);

        m_renderTexture->Update();
    }

    void DeferredRenderer::FindLights(Scene& scene, Camera& camera)
    {
        m_shadowCastingDirectionalLight = nullptr;

        // Prepare the light buffer with a single directional light for now
        LightBuffer lightBuffer;
        lightBuffer.ambientColor = m_scene.GetAmbientColor();
        lightBuffer.lightCount = 0;

        for (auto* light : scene.GetAllLights())
        {
            if (lightBuffer.lightCount >= MAX_LIGHTS)
            {
                break;  // Avoid exceeding the maximum number of lights
            }

            if (!light->IsVisible())
            {
                continue;
            }

            if (light->GetType() == LightType::Directional && m_shadowCastingDirectionalLight == nullptr)
            {
                // Check if the light is a shadow-casting directional light
                if (light->IsCastingShadows())
                {
                    m_shadowCastingDirectionalLight = light;
                }
            }

            if (light->GetType() != LightType::Directional)
            {
                // Check if light is in the camera's view frustum
                if (!camera.IsVisible(light->GetBoundingBox()))
                {
                    continue;  // Skip lights that are not in view
                }
            }

            const Vector4& color = light->GetColor();

            ShaderLight& bufferedLight = lightBuffer.lights[lightBuffer.lightCount++];
            bufferedLight.position = light->GetDerivedPosition();
            bufferedLight.color = Vector3(color.x, color.y, color.z);
            bufferedLight.intensity = light->GetIntensity();
            bufferedLight.range = light->GetRange();
            bufferedLight.spotAngle = 0.0f;
            bufferedLight.direction = light->GetDirection();
            bufferedLight.shadowMap = light->IsCastingShadows() ? 1 : 0;    // TODO: Shadow map index

            // Set up a directional light
            switch (light->GetType())
            {
            case LightType::Directional:
                bufferedLight.position = Vector3(0.0f, 0.0f, 0.0f);
                bufferedLight.range = 0.0f;  // Directional lights don't have a range
                bufferedLight.type = 1;
                break;
            case LightType::Point:
                bufferedLight.type = 0;
                break;

            case LightType::Spot:
                bufferedLight.type = 2;
                bufferedLight.spotAngle = light->GetOuterConeAngle();  // Not used for directional lights
                break;
            }
        }

        // Update the light buffer
        m_lightBuffer->Update(&lightBuffer);
    }

    void DeferredRenderer::RenderShadowMap(Scene& scene, Camera& camera)
    {
        if (m_shadowCastingDirectionalLight == nullptr)
        {
            return;
        }

        m_shadowCameraSetup->SetupShadowCamera(scene, camera, *m_shadowCastingDirectionalLight, *m_shadowCameras[0]);

        // Setup some depth bias settings
        m_device.SetDepthBias(m_depthBias);
        m_device.SetSlopeScaledDepthBias(m_slopeScaledDepthBias);
        m_device.SetDepthBiasClamp(m_depthBiasClamp);
        
        // Update the shadow buffer with legacy single cascade data
        ShadowBuffer buffer{};
        buffer.cascadeViewProjections[0] = (m_shadowCameras[0]->GetProjectionMatrix() * m_shadowCameras[0]->GetViewMatrix());
        buffer.cascadeSplitDistances = Vector4(camera.GetFarClipDistance(), 0.0f, 0.0f, 0.0f);
        buffer.shadowBias = m_shadowBias;
        buffer.normalBiasScale = m_normalBiasScale;
        buffer.shadowSoftness = m_shadowSoftness;
        buffer.blockerSearchRadius = m_blockerSearchRadius;
        buffer.lightSize = m_lightSize;
        buffer.cascadeCount = 1;
        buffer.debugCascades = m_debugCascades ? 1 : 0;
        buffer.cascadeBlendFactor = 0.0f;
        m_shadowBuffer->Update(&buffer);

        // Render the shadow map
        m_cascadeShadowMaps[0]->Activate();
        m_cascadeShadowMaps[0]->Clear(ClearFlags::Depth);
        scene.Render(*m_shadowCameras[0], PixelShaderType::ShadowMap);
        m_cascadeShadowMaps[0]->Update();

        // Reset
        m_device.SetDepthBias(0);
        m_device.SetSlopeScaledDepthBias(0);
        m_device.SetDepthBiasClamp(0);
    }

    void DeferredRenderer::RenderCascadedShadowMaps(Scene& scene, Camera& camera)
    {
        if (m_shadowCastingDirectionalLight == nullptr)
        {
            return;
        }

        // Setup all cascades
        m_cascadedShadowSetup->SetupCascades(scene, camera, *m_shadowCastingDirectionalLight, m_shadowCameras);

        // Get cascade data
        const auto& cascades = m_cascadedShadowSetup->GetCascades();
        const auto& config = m_cascadedShadowSetup->GetConfig();

        // Setup depth bias
        m_device.SetDepthBias(m_depthBias);
        m_device.SetSlopeScaledDepthBias(m_slopeScaledDepthBias);
        m_device.SetDepthBiasClamp(m_depthBiasClamp);

        // Render each cascade
        for (uint32 i = 0; i < NUM_SHADOW_CASCADES; ++i)
        {
            if (!m_shadowCameras[i])
            {
                continue;
            }

            m_cascadeShadowMaps[i]->Activate();
            m_cascadeShadowMaps[i]->Clear(ClearFlags::Depth);
            scene.Render(*m_shadowCameras[i], PixelShaderType::ShadowMap);
            m_cascadeShadowMaps[i]->Update();
        }

        // Update the shadow buffer with cascade data
        // Use the actual shadow camera matrices (what we rendered with) instead of computed ones
        ShadowBuffer buffer{};
        for (uint32 i = 0; i < NUM_SHADOW_CASCADES; ++i)
        {
            // Use the shadow camera's actual view-projection matrix
            buffer.cascadeViewProjections[i] = m_shadowCameras[i]->GetProjectionMatrix() * m_shadowCameras[i]->GetViewMatrix();
        }
        buffer.cascadeSplitDistances = Vector4(
            cascades[0].splitDistance,
            cascades[1].splitDistance,
            cascades[2].splitDistance,
            cascades[3].splitDistance
        );
        buffer.shadowBias = m_shadowBias;
        buffer.normalBiasScale = m_normalBiasScale;
        buffer.shadowSoftness = m_shadowSoftness;
        buffer.blockerSearchRadius = m_blockerSearchRadius;
        buffer.lightSize = m_lightSize;
        buffer.cascadeCount = NUM_SHADOW_CASCADES;
        buffer.debugCascades = m_debugCascades ? 1 : 0;
        buffer.cascadeBlendFactor = config.cascadeBlendFactor;
        m_shadowBuffer->Update(&buffer);

        // Reset depth bias
        m_device.SetDepthBias(0);
        m_device.SetSlopeScaledDepthBias(0);
        m_device.SetDepthBiasClamp(0);
    }

    TexturePtr DeferredRenderer::GetFinalRenderTarget() const
    {
        return m_renderTexture;
    }

    void DeferredRenderer::SetShadowMapSize(const uint16 size)
    {
        if (m_shadowMapSize == size || size == 0)
        {
            return;
        }

        m_shadowMapSize = size;
        
        // Resize all cascade shadow maps
        for (uint32 i = 0; i < NUM_SHADOW_CASCADES; ++i)
        {
            if (m_cascadeShadowMaps[i])
            {
                m_cascadeShadowMaps[i]->Resize(m_shadowMapSize, m_shadowMapSize);
                m_cascadeShadowMaps[i]->ApplyPendingResize();
            }
        }
        
        // Update CSM config
        if (m_cascadedShadowSetup)
        {
            m_cascadedShadowSetup->GetConfig().shadowMapSize = size;
        }
    }
}
