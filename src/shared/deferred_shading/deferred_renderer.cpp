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
        Matrix4 lightViewProjection;
        float shadowBias;           // Depth bias to prevent shadow acne
        float normalBiasScale;      // Bias scale factor based on normal
        float shadowSoftness;       // Controls general softness of shadows  
        float blockerSearchRadius;  // Search radius for the blocker search phase
        float lightSize;            // Controls the size of the virtual light (larger = softer shadows)
        Vector3 padding;
    };

    // CSM Shadow Buffer for multiple cascades
    struct alignas(16) CSMShadowBuffer
    {
        Matrix4 cascadeViewProjection[4];  // Up to 4 cascade matrices
        Vector4 cascadeSplits;             // Split distances for cascades (x,y,z,w)
        float shadowBias;
        float normalBiasScale;
        float shadowSoftness;
        float blockerSearchRadius;
        float lightSize;
        uint32 cascadeCount;
        Vector2 padding;
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
        m_csmCameraSetup = std::make_shared<CascadedShadowCameraSetup>();
        
#ifdef WIN32
		m_deferredLightVs = m_device.CreateShader(ShaderType::VertexShader, g_VS_DeferredLighting, std::size(g_VS_DeferredLighting));
        m_deferredLightPs = m_device.CreateShader(ShaderType::PixelShader, g_PS_DeferredLighting, std::size(g_PS_DeferredLighting));
#endif
          // Create the light buffer
        m_lightBuffer = m_device.CreateConstantBuffer(sizeof(LightBuffer), nullptr);
        m_shadowBuffer = m_device.CreateConstantBuffer(sizeof(ShadowBuffer), nullptr);
        m_csmShadowBuffer = m_device.CreateConstantBuffer(sizeof(CSMShadowBuffer), nullptr);

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
		// Create a high-resolution shadow map for better detail
		m_shadowMapRT = m_device.CreateRenderTexture("ShadowMap", m_shadowMapSize, m_shadowMapSize, RenderTextureFlags::HasDepthBuffer | RenderTextureFlags::ShaderResourceView);

        // Setup shadow camera
		m_shadowCameraNode = m_scene.GetRootSceneNode().CreateChildSceneNode("__ShadowCameraNode__");
		m_shadowCamera = m_scene.CreateCamera("__DeferredShadowCamera__");
		m_shadowCameraNode->AttachObject(*m_shadowCamera);

        // Initialize CSM resources
        InitializeCascadedShadowMaps();
#ifdef WIN32        // TODO: Fix me: Move me to graphicsd3d11
        D3D11_SAMPLER_DESC sampDesc = {};
        // Using higher quality comparison filtering for shadows
        sampDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
        // Use border addressing mode to avoid artifacts at shadow map edges
        sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
        sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
        sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
        sampDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;  // Shadow test: fragment depth <= stored depth
        // White border color to avoid darkening at edges
        sampDesc.BorderColor[0] = 1.0f;
        sampDesc.BorderColor[1] = 1.0f;
        sampDesc.BorderColor[2] = 1.0f;
        sampDesc.BorderColor[3] = 1.0f;
        sampDesc.MinLOD = 0;
        sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
        // Using standard filtering - anisotropic isn't necessary for shadow mapping
        sampDesc.MaxAnisotropy = 1;
        sampDesc.MipLODBias = 0;

		GraphicsDeviceD3D11& d3ddev = (GraphicsDeviceD3D11&)device;
        ID3D11Device& d3d11dev = d3ddev;
        d3d11dev.CreateSamplerState(&sampDesc, &m_shadowSampler);
#endif
    }

    DeferredRenderer::~DeferredRenderer()
    {
        if (m_shadowCamera)
        {
            m_scene.DestroyCamera(*m_shadowCamera);
            m_shadowCamera = nullptr;
        }

        if (m_shadowCameraNode)
        {
            m_scene.DestroySceneNode(*m_shadowCameraNode);
            m_shadowCameraNode = nullptr;
        }

        // Clean up CSM resources
        for (auto* camera : m_cascadeShadowCameras)
        {
            if (camera)
            {
                m_scene.DestroyCamera(*camera);
            }
        }
        m_cascadeShadowCameras.clear();

        for (auto* node : m_cascadeShadowCameraNodes)
        {
            if (node)
            {
                m_scene.DestroySceneNode(*node);
            }
        }
        m_cascadeShadowCameraNodes.clear();
        m_cascadeShadowMaps.clear();
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

        if (m_shadowCastingDirecitonalLight)
        {
			// Render the shadow map for the directional light
			RenderShadowMap(scene, camera);
        }

        // Render cascaded shadow maps if enabled
        if (m_csmEnabled && m_shadowCastingDirecitonalLight)
        {
            RenderCascadedShadowMaps(scene, camera);
        }
        else if (m_shadowCastingDirecitonalLight)
        {
            // Fall back to regular shadow mapping
            RenderShadowMap(scene, camera);
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
        m_gBuffer.GetEmissiveRT().Bind(ShaderType::PixelShader, 3);        m_gBuffer.GetViewRayRT().Bind(ShaderType::PixelShader, 4);
        
        if (m_csmEnabled && !m_cascadeShadowMaps.empty())
        {
            // Bind cascade shadow maps
            for (uint32 i = 0; i < m_cascadeCount && i < m_cascadeShadowMaps.size(); ++i)
            {
                m_cascadeShadowMaps[i]->Bind(ShaderType::PixelShader, 5 + i);
            }
        }
        else
        {
            // Bind regular shadow map
            m_shadowMapRT->Bind(ShaderType::PixelShader, 5);
        }

        // Set the vertex format for the full-screen quad
        m_device.SetVertexFormat(VertexFormat::PosColorTex1);

        // Bind the G-Buffer textures to the lighting material
        m_deferredLightVs->Set();
        m_deferredLightPs->Set();

        // Set the topology type for the full-screen quad
        m_device.SetTopologyType(TopologyType::TriangleList);

        // Render a full screen quad
    	m_quadBuffer->Set(0);        // Bind buffer to stage        scene.GetCameraBuffer()->BindToStage(ShaderType::PixelShader, 1);
        m_lightBuffer->BindToStage(ShaderType::PixelShader, 2);
        
        if (m_csmEnabled)
        {
            m_csmShadowBuffer->BindToStage(ShaderType::PixelShader, 4);
        }
        else
        {
            m_shadowBuffer->BindToStage(ShaderType::PixelShader, 3);
        }

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
        m_shadowCastingDirecitonalLight = nullptr;

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

            if (light->GetType() == LightType::Directional && m_shadowCastingDirecitonalLight == nullptr)
            {
                // Check if the light is a shadow-casting directional light
                if (light->IsCastingShadows())
                {
                    m_shadowCastingDirecitonalLight = light;
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
        if (m_shadowCastingDirecitonalLight == nullptr)
        {
            return;
        }

        m_shadowCameraSetup->SetupShadowCamera(scene, camera, *m_shadowCastingDirecitonalLight, *m_shadowCamera);

        // Setup hardware depth bias settings (helps with shadow acne)
        m_device.SetDepthBias(m_depthBias);
        m_device.SetSlopeScaledDepthBias(m_slopeScaledDepthBias);
        m_device.SetDepthBiasClamp(m_depthBiasClamp);
        
        // Update the shadow buffer with the light view-projection matrix and shadow parameters
        ShadowBuffer buffer;
        buffer.lightViewProjection = (m_shadowCamera->GetProjectionMatrix() * m_shadowCamera->GetViewMatrix());
        buffer.shadowBias = m_shadowBias;
        buffer.normalBiasScale = m_normalBiasScale;
        buffer.shadowSoftness = m_shadowSoftness;
        buffer.blockerSearchRadius = m_blockerSearchRadius;
        buffer.lightSize = m_lightSize;
        m_shadowBuffer->Update(&buffer);

        // Render the shadow map
        m_shadowMapRT->Activate();
        m_shadowMapRT->Clear(ClearFlags::Depth);
        scene.Render(*m_shadowCamera, PixelShaderType::ShadowMap);
        m_shadowMapRT->Update();

        // Reset depth bias settings
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
        m_shadowMapRT->Resize(m_shadowMapSize, m_shadowMapSize);
        m_shadowMapRT->ApplyPendingResize();

        // Also resize CSM shadow maps if they exist
        for (auto& cascadeMap : m_cascadeShadowMaps)
        {
            if (cascadeMap)
            {
                cascadeMap->Resize(m_shadowMapSize / 2, m_shadowMapSize / 2); // Smaller per-cascade resolution
                cascadeMap->ApplyPendingResize();
            }
        }
    }

    void DeferredRenderer::SetCascadedShadowMapsEnabled(bool enabled)
    {
        if (m_csmEnabled != enabled)
        {
            m_csmEnabled = enabled;
            if (enabled)
            {
                InitializeCascadedShadowMaps();
            }
        }
    }

    void DeferredRenderer::SetCascadeCount(uint32 count)
    {
        count = std::clamp(count, 1u, 4u);
        if (m_cascadeCount != count)
        {
            m_cascadeCount = count;
            if (m_csmCameraSetup)
            {
                m_csmCameraSetup->SetCascadeCount(count);
            }
            InitializeCascadedShadowMaps(); // Recreate resources with new count
        }
    }

    uint32 DeferredRenderer::GetCascadeCount() const
    {
        return m_cascadeCount;
    }

    const std::vector<float>& DeferredRenderer::GetCascadeSplits() const
    {
        if (m_csmCameraSetup)
        {
            return m_csmCameraSetup->GetCascadeSplits();
        }
        static std::vector<float> empty;
        return empty;
    }

    void DeferredRenderer::SetSplitLambda(float lambda)
    {
        if (m_csmCameraSetup)
        {
            m_csmCameraSetup->SetSplitLambda(lambda);
        }
    }    float DeferredRenderer::GetSplitLambda() const
    {
        if (m_csmCameraSetup)
        {
            return m_csmCameraSetup->GetSplitLambda();
        }
        return 0.5f;
    }

    void DeferredRenderer::InitializeCascadedShadowMaps()
    {
        // Clean up existing resources
        for (auto* camera : m_cascadeShadowCameras)
        {
            if (camera)
            {
                m_scene.DestroyCamera(*camera);
            }
        }
        m_cascadeShadowCameras.clear();

        for (auto* node : m_cascadeShadowCameraNodes)
        {
            if (node)
            {
                m_scene.DestroySceneNode(*node);
            }
        }
        m_cascadeShadowCameraNodes.clear();
        m_cascadeShadowMaps.clear();

        if (!m_csmEnabled)
        {
            return;
        }

        // Create resources for each cascade
        uint32 cascadeMapSize = m_shadowMapSize / 2; // Smaller resolution per cascade
        
        for (uint32 i = 0; i < m_cascadeCount; ++i)
        {
            // Create shadow map for this cascade
            String cascadeName = "CascadeShadowMap_" + std::to_string(i);
            auto cascadeMap = m_device.CreateRenderTexture(
                cascadeName,
                cascadeMapSize,
                cascadeMapSize,
                RenderTextureFlags::HasDepthBuffer | RenderTextureFlags::ShaderResourceView
            );
            m_cascadeShadowMaps.push_back(cascadeMap);

            // Create camera node and camera for this cascade
            String nodeName = "__CascadeShadowCameraNode_" + std::to_string(i) + "__";
            String cameraName = "__CascadeShadowCamera_" + std::to_string(i) + "__";
            
            auto* cameraNode = m_scene.GetRootSceneNode().CreateChildSceneNode(nodeName);
            auto* camera = m_scene.CreateCamera(cameraName);
            cameraNode->AttachObject(*camera);

            m_cascadeShadowCameraNodes.push_back(cameraNode);
            m_cascadeShadowCameras.push_back(camera);
        }
    }

    void DeferredRenderer::RenderCascadedShadowMaps(Scene& scene, Camera& camera)
    {
        if (!m_shadowCastingDirecitonalLight || !m_csmEnabled || m_cascadeShadowCameras.empty())
        {
            return;
        }

        // Setup cascade cameras using CSM camera setup
        m_csmCameraSetup->SetupCascadedShadowCameras(scene, camera, *m_shadowCastingDirecitonalLight, m_cascadeShadowCameras);

        // Setup hardware depth bias settings
        m_device.SetDepthBias(m_depthBias);
        m_device.SetSlopeScaledDepthBias(m_slopeScaledDepthBias);
        m_device.SetDepthBiasClamp(m_depthBiasClamp);

        // Render each cascade
        for (uint32 i = 0; i < m_cascadeCount && i < m_cascadeShadowMaps.size(); ++i)
        {
            m_cascadeShadowMaps[i]->Activate();
            m_cascadeShadowMaps[i]->Clear(ClearFlags::Depth);
            scene.Render(*m_cascadeShadowCameras[i], PixelShaderType::ShadowMap);
            m_cascadeShadowMaps[i]->Update();
        }

        // Reset depth bias settings
        m_device.SetDepthBias(0);
        m_device.SetSlopeScaledDepthBias(0);
        m_device.SetDepthBiasClamp(0);        // Update shadow buffer with cascade matrices
        if (!m_cascadeShadowCameras.empty())
        {
            CSMShadowBuffer csmBuffer = {};
            
            // Fill cascade matrices
            const auto& cascades = m_csmCameraSetup->GetCascades();
            for (uint32 i = 0; i < m_cascadeCount && i < 4; ++i)
            {
                if (i < cascades.size())
                {
                    csmBuffer.cascadeViewProjection[i] = cascades[i].viewProjectionMatrix;
                }
            }
            
            // Fill cascade split distances
            const auto& splits = m_csmCameraSetup->GetCascadeSplits();
            if (splits.size() >= 4)
            {
                csmBuffer.cascadeSplits = Vector4(splits[1], splits[2], splits[3], 
                                                  splits.size() > 4 ? splits[4] : splits.back());
            }
            
            csmBuffer.shadowBias = m_shadowBias;
            csmBuffer.normalBiasScale = m_normalBiasScale;
            csmBuffer.shadowSoftness = m_shadowSoftness;
            csmBuffer.blockerSearchRadius = m_blockerSearchRadius;
            csmBuffer.lightSize = m_lightSize;
            csmBuffer.cascadeCount = m_cascadeCount;
            
            m_csmShadowBuffer->Update(&csmBuffer);
        }
    }
}
