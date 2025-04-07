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
        // Create the lighting material
        auto material = std::make_shared<Material>("DeferredLighting");
        m_lightingMaterial = material;

        // Create a material compiler
        auto materialCompiler = m_device.CreateMaterialCompiler();
        auto shaderCompiler = m_device.CreateShaderCompiler();

        // Set the material to be unlit (no lighting calculations in the shader)
        materialCompiler->SetLit(false);

        // Compile the material
        materialCompiler->Compile(*material, *shaderCompiler);

        // Set the material to be two-sided
        material->SetTwoSided(true);

        // Set the material to be opaque
        material->SetType(MaterialType::Opaque);

        // Set the material to not write to the depth buffer
        material->SetDepthWriteEnabled(false);

        // Set the material to not test the depth buffer
        material->SetDepthTestEnabled(false);

        // Create the light buffer
        m_lightBuffer = m_device.CreateConstantBuffer(sizeof(LightBuffer), nullptr);
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

        // Prepare the light buffer with a single directional light for now
        LightBuffer lightBuffer;
        lightBuffer.lightCount = 1;
        lightBuffer.ambientColor = m_ambientColor;

        // Set up a directional light
        ShaderLight& directionalLight = lightBuffer.lights[0];
        directionalLight.position = Vector3(0.0f, 0.0f, 0.0f);
        directionalLight.color = Vector3(1.0f, 1.0f, 1.0f);
        directionalLight.intensity = 1.0f;
        directionalLight.range = 0.0f;  // Directional lights don't have a range
        directionalLight.direction = Vector3(0.5f, -1.0f, 0.5f).NormalizedCopy();
        directionalLight.type = 1;  // Directional light
        directionalLight.spotAngle = 0.0f;  // Not used for directional lights

        // Update the light buffer
        m_lightBuffer->Update(&lightBuffer);

        // Bind the G-Buffer textures to the lighting material
        m_device.BindTexture(m_gBuffer.GetAlbedoRT().StoreToTexture(), ShaderType::PixelShader, 0);
        m_device.BindTexture(m_gBuffer.GetNormalRT().StoreToTexture(), ShaderType::PixelShader, 1);
        m_device.BindTexture(m_gBuffer.GetMaterialRT().StoreToTexture(), ShaderType::PixelShader, 2);
        m_device.BindTexture(m_gBuffer.GetEmissiveRT().StoreToTexture(), ShaderType::PixelShader, 3);
        m_device.BindTexture(m_gBuffer.GetDepthRT().StoreToTexture(), ShaderType::PixelShader, 4);

        // Apply the lighting material
        m_lightingMaterial->Apply(m_device, MaterialDomain::Surface);

        // Bind the light buffer
        m_lightBuffer->BindToStage(ShaderType::PixelShader, 1);

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
