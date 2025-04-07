// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "g_buffer.h"
#include "graphics_device.h"

namespace mmo
{
    GBuffer::GBuffer(uint16 width, uint16 height)
        : RenderTarget("GBuffer", width, height)
    {
        Initialize();
    }

    void GBuffer::Initialize()
    {
        auto& gx = GraphicsDevice::Get();

        // Create render textures for the G-Buffer
        m_albedoRT = gx.CreateRenderTexture("GBuffer_Albedo", m_width, m_height, PixelFormat::R8G8B8A8);
        m_normalRT = gx.CreateRenderTexture("GBuffer_Normal", m_width, m_height, PixelFormat::R8G8B8A8);
        m_materialPropertiesRT = gx.CreateRenderTexture("GBuffer_MaterialProperties", m_width, m_height, PixelFormat::R8G8B8A8);
        m_emissiveRT = gx.CreateRenderTexture("GBuffer_Emissive", m_width, m_height, PixelFormat::R8G8B8A8);
        m_depthRT = gx.CreateRenderTexture("GBuffer_Depth", m_width, m_height, PixelFormat::R32G32B32A32);
    }

    void GBuffer::Activate()
    {
        RenderTarget::Activate();

        // Set up multiple render targets
        RenderTexturePtr renderTargets[] = {
            m_albedoRT,
            m_normalRT,
            m_materialPropertiesRT,
            m_emissiveRT
        };

        // Set multiple render targets
        auto& gx = GraphicsDevice::Get();
        gx.SetRenderTargets(renderTargets, 4);
    }

    void GBuffer::Clear(ClearFlags flags)
    {
        m_albedoRT->Clear(flags);
        m_normalRT->Clear(flags);
        m_materialPropertiesRT->Clear(flags);
        m_emissiveRT->Clear(flags);
        m_depthRT->Clear(flags);
    }

    void GBuffer::Update()
    {
        m_albedoRT->Update();
        m_normalRT->Update();
        m_materialPropertiesRT->Update();
        m_emissiveRT->Update();
        m_depthRT->Update();
    }

    void GBuffer::Resize(uint16 width, uint16 height)
    {
        RenderTarget::Resize(width, height);

        m_albedoRT->Resize(width, height);
        m_normalRT->Resize(width, height);
        m_materialPropertiesRT->Resize(width, height);
        m_emissiveRT->Resize(width, height);
        m_depthRT->Resize(width, height);
    }

    void GBuffer::BindForReading()
    {
        auto& gx = GraphicsDevice::Get();

        // Bind G-Buffer textures for reading in the lighting pass
        gx.BindTexture(m_albedoRT, ShaderType::PixelShader, 0);
        gx.BindTexture(m_normalRT, ShaderType::PixelShader, 1);
        gx.BindTexture(m_materialPropertiesRT, ShaderType::PixelShader, 2);
        gx.BindTexture(m_emissiveRT, ShaderType::PixelShader, 3);
        gx.BindTexture(m_depthRT, ShaderType::PixelShader, 4);
    }
}
