// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "deferred_renderer.h"
#include "graphics/graphics_device.h"
#include "graphics/material_compiler.h"
#include "graphics/shader_compiler.h"
#include "scene_graph/camera.h"
#include "scene_graph/scene_node.h"
#include "scene_graph/render_queue.h"
#include "log/default_log_levels.h"

namespace mmo
{
    DeferredRenderer::DeferredRenderer(GraphicsDevice& device, uint32 width, uint32 height)
        : m_device(device)
        , m_gBuffer(device, width, height)
    {
    }

    void DeferredRenderer::Resize(uint32 width, uint32 height)
    {
        // Resize the G-Buffer
        m_gBuffer.Resize(width, height);
    }

    void DeferredRenderer::Render(Scene& scene, Camera& camera)
    {
        // Render the geometry pass
        RenderGeometryPass(scene, camera);

        // Render the lighting pass
        RenderLightingPass(scene, camera);
    }

    void DeferredRenderer::RenderGeometryPass(Scene& scene, Camera& camera)
    {
        // Bind the G-Buffer
        m_gBuffer.Bind();

        // Render the scene using the camera
        scene.Render(camera, PixelShaderType::GBuffer);
    }

    void DeferredRenderer::RenderLightingPass(Scene& scene, Camera& camera)
    {
        // Unbind the G-Buffer
        m_gBuffer.Unbind();

        // Set the render target to the back buffer
        m_device.GetAutoCreatedWindow()->Activate();

        // Disable depth testing and writing
        m_device.SetDepthEnabled(false);
        m_device.SetDepthWriteEnabled(false);

        // TODO: Apply light vertex- and pixel shaders and constant buffers and required shader resources


        // Bind the G-Buffer textures to the lighting material
        m_device.BindTexture(m_gBuffer.GetAlbedoRT().StoreToTexture(), ShaderType::PixelShader, 0);
        m_device.BindTexture(m_gBuffer.GetNormalRT().StoreToTexture(), ShaderType::PixelShader, 1);
        m_device.BindTexture(m_gBuffer.GetMaterialRT().StoreToTexture(), ShaderType::PixelShader, 2);
        m_device.BindTexture(m_gBuffer.GetEmissiveRT().StoreToTexture(), ShaderType::PixelShader, 3);
        m_device.BindTexture(m_gBuffer.GetDepthRT().StoreToTexture(), ShaderType::PixelShader, 4);

        // Set the vertex format for the full-screen quad
        m_device.SetVertexFormat(VertexFormat::PosColorTex1);

        // Set the topology type for the full-screen quad
        m_device.SetTopologyType(TopologyType::TriangleList);

        // Draw a full-screen quad
        m_device.Draw(6);
    }

    TexturePtr DeferredRenderer::GetFinalRenderTarget() const
    {
        // In this implementation, we're rendering directly to the back buffer,
        // so we don't have a separate final render target texture.
        // In a more complex implementation, we might render to a separate texture
        // and return that here.
        return nullptr;
    }
}
