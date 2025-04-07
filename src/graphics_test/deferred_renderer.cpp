// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "deferred_renderer.h"
#include "shared/graphics/graphics_device.h"

namespace mmo
{
    DeferredRenderer::DeferredRenderer(uint32 width, uint32 height)
        : m_width(width)
        , m_height(height)
    {
        Initialize();
    }

    void DeferredRenderer::Initialize()
    {
        auto& gx = GraphicsDevice::Get();

        // Create G-Buffer
        m_gBuffer = std::make_shared<GBuffer>(m_width, m_height);

        // Create final render target
        m_finalRenderTarget = gx.CreateRenderTexture("FinalRenderTarget", m_width, m_height);

        // Create full screen quad for deferred passes
        const POS_COL_TEX_VERTEX vertices[6] = {
            { Vector3(-1.0f, -1.0f, 0.0f), Color::White, {0.0f, 1.0f}},
            { Vector3(1.0f, -1.0f, 0.0f), Color::White, {1.0f, 1.0f}},
            { Vector3(-1.0f, 1.0f, 0.0f), Color::White, {0.0f, 0.0f}},
            { Vector3(-1.0f, 1.0f, 0.0f), Color::White, {0.0f, 0.0f}},
            { Vector3(1.0f, -1.0f, 0.0f), Color::White, {1.0f, 1.0f}},
            { Vector3(1.0f, 1.0f, 0.0f), Color::White, {1.0f, 0.0f}}
        };

        m_fullScreenQuadBuffer = gx.CreateVertexBuffer(6, sizeof(POS_COL_TEX_VERTEX), BufferUsage::Static, vertices);
    }

    void DeferredRenderer::Render(Scene& scene, Camera& camera)
    {
        // Geometry pass - render scene to G-Buffer
        RenderGeometryPass(scene, camera);

        // Lighting pass - apply lighting to G-Buffer
        RenderLightingPass(scene, camera);

        // Composition pass - combine results
        RenderCompositionPass();
    }

    void DeferredRenderer::RenderGeometryPass(Scene& scene, Camera& camera)
    {
        auto& gx = GraphicsDevice::Get();

        // Capture current state
        gx.CaptureState();

        // Activate G-Buffer for writing
        m_gBuffer->Activate();
        m_gBuffer->Clear(ClearFlags::All);

        // Render scene to G-Buffer
        scene.Render(camera);

        // Update G-Buffer
        m_gBuffer->Update();

        // Restore state
        gx.RestoreState();
    }

    void DeferredRenderer::RenderLightingPass(Scene& scene, Camera& camera)
    {
        auto& gx = GraphicsDevice::Get();

        // Capture current state
        gx.CaptureState();

        // Activate final render target
        m_finalRenderTarget->Activate();
        m_finalRenderTarget->Clear(ClearFlags::All);

        // Set up rendering state
        gx.SetTransformMatrix(World, Matrix4::Identity);
        gx.SetTransformMatrix(View, Matrix4::Identity);
        gx.SetTransformMatrix(Projection, Matrix4::Identity);
        gx.SetVertexFormat(VertexFormat::PosColorTex1);
        gx.SetTopologyType(TopologyType::TriangleList);
        gx.SetTextureFilter(TextureFilter::None);
        gx.SetTextureAddressMode(TextureAddressMode::Clamp);

        // Bind G-Buffer textures
        m_gBuffer->BindForReading();

        // Get all lights from the scene
        auto lights = scene.GetAllLights();

        // Create lighting shader if not already created
        if (!m_lightingVertexShader)
        {
            // Load vertex shader
            std::vector<uint8> vsCode;
            if (LoadShaderFromFile("data/client/DeferredLightingVS.hlsl", ShaderType::VertexShader, vsCode))
            {
                m_lightingVertexShader = gx.CreateShader(ShaderType::VertexShader, vsCode.data(), vsCode.size());
            }

            // Load pixel shader
            std::vector<uint8> psCode;
            if (LoadShaderFromFile("data/client/DeferredLighting.hlsl", ShaderType::PixelShader, psCode))
            {
                m_lightingPixelShader = gx.CreateShader(ShaderType::PixelShader, psCode.data(), psCode.size());
            }

            // Create light buffer
            m_lightBuffer = gx.CreateConstantBuffer(sizeof(LightBuffer), nullptr);
        }

        // Update light buffer with scene lights
        if (m_lightBuffer)
        {
            LightBuffer lightData;
            lightData.LightCount = std::min(static_cast<uint32>(lights.size()), 16u);
            lightData.AmbientColor = Vector3(0.1f, 0.1f, 0.1f);

            for (uint32 i = 0; i < lightData.LightCount; ++i)
            {
                auto& light = lights[i];
                Vector3 direction;
                Color color;
                float intensity;

                light->GetLightParameters(direction, color, intensity);

                lightData.Lights[i].Direction = direction;
                lightData.Lights[i].Color = Vector4(color.GetRed(), color.GetGreen(), color.GetBlue(), intensity);
                lightData.Lights[i].Type = static_cast<uint32>(light->GetType());
                lightData.Lights[i].CastsShadow = light->CastsShadows() ? 1 : 0;

                if (light->GetType() == LightType::Directional)
                {
                    lightData.Lights[i].Position = Vector3::Zero;
                    lightData.Lights[i].Range = 0.0f;
                }
                else
                {
                    lightData.Lights[i].Position = light->GetDerivedPosition();
                    lightData.Lights[i].Range = light->GetAttenuationRange();
                }
            }

            m_lightBuffer->Update(&lightData);
        }

        // Bind shaders and constant buffers
        if (m_lightingVertexShader && m_lightingPixelShader)
        {
            m_lightingVertexShader->Set();
            m_lightingPixelShader->Set();

            if (m_lightBuffer)
            {
                m_lightBuffer->BindToStage(ShaderType::PixelShader, 1);
            }
        }
        
        // Draw full screen quad
        m_fullScreenQuadBuffer->Set(0);
        gx.Draw(6);

        // Update final render target
        m_finalRenderTarget->Update();

        // Restore state
        gx.RestoreState();
    }

    bool DeferredRenderer::LoadShaderFromFile(const std::string& filename, ShaderType type, std::vector<uint8>& outCode)
    {
        // Open the file
        std::ifstream file(filename, std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            return false;
        }

        // Get file size and allocate buffer
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        outCode.resize(size);

        // Read file content
        if (!file.read(reinterpret_cast<char*>(outCode.data()), size))
        {
            return false;
        }

        return true;
    }

    void DeferredRenderer::RenderCompositionPass()
    {
        // For now, the lighting pass directly renders to the final render target
        // In the future, this could be used for post-processing effects
    }

    void DeferredRenderer::Resize(uint32 width, uint32 height)
    {
        m_width = width;
        m_height = height;

        // Recreate G-Buffer and final render target with new dimensions
        Initialize();
    }
}
