// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "shadow_camera_setup.h"
#include "frame_ui/geometry_buffer.h"
#include "graphics/material_compiler.h"
#include "graphics/g_buffer.h"
#include "graphics/material.h"
#include "scene_graph/scene.h"
#include "scene_graph/light.h"

#ifdef _WIN32
#   include <d3d11.h>
#   include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#endif

namespace mmo
{
    /// @brief Class that implements deferred rendering.
    class DeferredRenderer
    {
    public:
        /// @brief Creates a new instance of the DeferredRenderer class.
        /// @param device The graphics device to use.
        /// @param width The width of the renderer.
        /// @param height The height of the renderer.
        DeferredRenderer(GraphicsDevice& device, Scene& scene, uint32 width, uint32 height);

        /// @brief Destructor.
        ~DeferredRenderer();

    public:
        /// @brief Resizes the renderer.
        /// @param width The new width of the renderer.
        /// @param height The new height of the renderer.
        void Resize(uint32 width, uint32 height);

        /// @brief Renders a scene using deferred rendering.
        /// @param scene The scene to render.
        /// @param camera The camera to use for rendering.
        void Render(Scene& scene, Camera& camera);

        /// @brief Gets the final render target.
        /// @return The final render target.
        [[nodiscard]] TexturePtr GetFinalRenderTarget() const;

        Camera* GetShadowCamera() const
        {
            return m_shadowCamera;
        }

        void SetDepthBias(float bias, float slope, float clamp)
        {
			m_depthBias = bias;
			m_slopeScaledDepthBias = slope;
			m_depthBiasClamp = clamp;
        }

    private:
        /// @brief Renders the geometry pass.
        /// @param scene The scene to render.
        /// @param camera The camera to use for rendering.
        void RenderGeometryPass(Scene& scene, Camera& camera);

        /// @brief Renders the lighting pass.
        /// @param scene The scene to render.
        /// @param camera The camera to use for rendering.
        void RenderLightingPass(Scene& scene, Camera& camera);

        void FindLights(Scene& scene, Camera& camera);

		void RenderShadowMap(Scene& scene, Camera& camera);

    public:
        /// @brief Maximum number of lights that can be processed in a single pass.
        static constexpr uint32 MAX_LIGHTS = 16;

    private:
        /// @brief The graphics device.
        GraphicsDevice& m_device;

        Scene& m_scene;

        /// @brief The G-Buffer.
        GBuffer m_gBuffer;

        /// @brief The light buffer.
        ConstantBufferPtr m_lightBuffer;

        ConstantBufferPtr m_shadowBuffer;

        /// @brief The ambient light color.
        Vector3 m_ambientColor{ 0.04f, 0.035f, 0.03f };

        ShaderPtr m_deferredLightVs;

        ShaderPtr m_deferredLightPs;

        VertexBufferPtr m_quadBuffer;

        RenderTexturePtr m_renderTexture;

        Light* m_shadowCastingDirecitonalLight;

        RenderTexturePtr m_shadowMapRT;

		SceneNode* m_shadowCameraNode{ nullptr };

        Camera* m_shadowCamera{ nullptr };

		ComPtr<ID3D11SamplerState> m_shadowSampler{ nullptr };

        std::shared_ptr<ShadowCameraSetup> m_shadowCameraSetup = nullptr;

        float m_depthBias = 250.0f;

		float m_slopeScaledDepthBias = 1.0f;

		float m_depthBiasClamp = 0.0f;
    };
}
