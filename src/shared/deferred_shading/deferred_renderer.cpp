// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "deferred_renderer.h"

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
        Vector3 padding;
    };

    // Light buffer structure that matches the one in the shader
    struct alignas(16) LightBuffer
    {
        uint32 lightCount;
        Vector3 ambientColor;
        ShaderLight lights[DeferredRenderer::MAX_LIGHTS];
    };

    DeferredRenderer::DeferredRenderer(GraphicsDevice& device, uint32 width, uint32 height)
        : m_device(device)
        , m_gBuffer(device, width, height)
    {
		m_deferredLightVs = m_device.CreateShader(ShaderType::VertexShader, g_VS_DeferredLighting, std::size(g_VS_DeferredLighting));
        m_deferredLightPs = m_device.CreateShader(ShaderType::PixelShader, g_PS_DeferredLighting, std::size(g_PS_DeferredLighting));

        // Create the light buffer
        m_lightBuffer = m_device.CreateConstantBuffer(sizeof(LightBuffer), nullptr);

        // Create the render texture
        m_renderTexture = m_device.CreateRenderTexture("DeferredOutput", width, height, RenderTextureFlags::HasColorBuffer | RenderTextureFlags::ShaderResourceView);
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

		m_shadowMapRT = m_device.CreateRenderTexture("ShadowMap", 2048, 2048, RenderTextureFlags::HasDepthBuffer | RenderTextureFlags::ShaderResourceView);
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

        // Bind the light buffer
        m_lightBuffer->BindToStage(ShaderType::PixelShader, 2);

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

        m_device.SetFillMode(FillMode::Solid);
        m_device.SetFaceCullMode(FaceCullMode::None);
        m_device.SetTextureAddressMode(TextureAddressMode::Clamp, TextureAddressMode::Clamp, TextureAddressMode::Clamp);
        m_device.SetTextureFilter(TextureFilter::Trilinear);

        // Draw a full-screen quad
        m_device.Draw(6);

        m_renderTexture->Update();
    }

    void DeferredRenderer::FindLights(Scene& scene, Camera& camera)
    {
        m_shadowCastingDirecitonalLight = nullptr;

        // Prepare the light buffer with a single directional light for now
        LightBuffer lightBuffer;
        lightBuffer.ambientColor = m_ambientColor;
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

        // For now we just render the exact scene just as depth. We need to configure a shadow camera though
        m_shadowMapRT->Activate();
		m_shadowMapRT->Clear(ClearFlags::Depth);

        scene.Render(camera, PixelShaderType::ShadowMap);
        m_shadowMapRT->Update();
    }

    TexturePtr DeferredRenderer::GetFinalRenderTarget() const
    {
        // In this implementation, we're rendering directly to the back buffer,
        // so we don't have a separate final render target texture.
        // In a more complex implementation, we might render to a separate texture
        // and return that here.
        return m_renderTexture;
    }
}
