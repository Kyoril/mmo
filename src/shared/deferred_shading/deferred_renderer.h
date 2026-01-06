// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "shadow_camera_setup.h"
#include "cascaded_shadow_camera_setup.h"
#include "frame_ui/geometry_buffer.h"
#include "graphics/material_compiler.h"
#include "graphics/g_buffer.h"
#include "graphics/material.h"
#include "scene_graph/scene.h"
#include "scene_graph/light.h"

#include <array>

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
            return m_shadowCameras[0];
        }

        /// @brief Gets the cascaded shadow camera setup.
        [[nodiscard]] CascadedShadowCameraSetup* GetCascadedShadowSetup() const
        {
            return m_cascadedShadowSetup.get();
        }

        /// @brief Enables or disables cascaded shadow maps.
        void SetCascadedShadowsEnabled(bool enabled) { m_useCascadedShadows = enabled; }

        /// @brief Checks if cascaded shadow maps are enabled.
        [[nodiscard]] bool IsCascadedShadowsEnabled() const { return m_useCascadedShadows; }

        /// @brief Enables or disables cascade debug visualization.
        void SetCascadeDebugVisualization(bool enabled) { m_debugCascades = enabled; }

        /// @brief Checks if cascade debug visualization is enabled.
        [[nodiscard]] bool IsCascadeDebugVisualizationEnabled() const { return m_debugCascades; }

        void SetDepthBias(float bias, float slope, float clamp)
        {
			m_depthBias = bias;
			m_slopeScaledDepthBias = slope;
			m_depthBiasClamp = clamp;
        }

        // Advanced shadow parameters getters and setters
        void SetShadowBias(float bias) { m_shadowBias = bias; }
        float GetShadowBias() const { return m_shadowBias; }

        void SetNormalBiasScale(float scale) { m_normalBiasScale = scale; }
        float GetNormalBiasScale() const { return m_normalBiasScale; }

        void SetShadowSoftness(float softness) { m_shadowSoftness = softness; }
        float GetShadowSoftness() const { return m_shadowSoftness; }

        void SetBlockerSearchRadius(float radius) { m_blockerSearchRadius = radius; }
        float GetBlockerSearchRadius() const { return m_blockerSearchRadius; }
          void SetLightSize(float size) { m_lightSize = size; }
        float GetLightSize() const { return m_lightSize; }
        
        void SetShadowMapSize(uint16 size);
        uint16 GetShadowMapSize() const { return m_shadowMapSize; }

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

        void RenderCascadedShadowMaps(Scene& scene, Camera& camera);

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

        ShaderPtr m_deferredLightVs;

        ShaderPtr m_deferredLightPs;

        VertexBufferPtr m_quadBuffer;

        RenderTexturePtr m_renderTexture;

        Light* m_shadowCastingDirectionalLight;

        /// @brief Shadow map render textures for each cascade.
        std::array<RenderTexturePtr, NUM_SHADOW_CASCADES> m_cascadeShadowMaps;

        /// @brief Legacy single shadow map (kept for compatibility).
        RenderTexturePtr m_shadowMapRT;

        /// @brief Shadow camera nodes for each cascade.
        std::array<SceneNode*, NUM_SHADOW_CASCADES> m_shadowCameraNodes{};

        /// @brief Shadow cameras for each cascade.
        std::array<Camera*, NUM_SHADOW_CASCADES> m_shadowCameras{};

#ifdef _WIN32
		ComPtr<ID3D11SamplerState> m_shadowSampler{ nullptr };
#endif

        /// @brief Cascaded shadow camera setup.
        std::shared_ptr<CascadedShadowCameraSetup> m_cascadedShadowSetup = nullptr;
        
        std::shared_ptr<ShadowCameraSetup> m_shadowCameraSetup = nullptr;

        /// @brief Whether to use cascaded shadow maps.
        bool m_useCascadedShadows = false;  // Temporarily disabled for debugging

        /// @brief Whether to show cascade debug colors.
        bool m_debugCascades = false;

        float m_depthBias = 100.0f;
		float m_slopeScaledDepthBias = 2.0f;
		float m_depthBiasClamp = 0.0f;

        // Advanced shadow parameters
        float m_shadowBias = 0.0001f;         // Depth bias in shadow space (reduced for less peter panning)
        float m_normalBiasScale = 0.02f;      // Normal-based bias scale factor (reduced)
        float m_shadowSoftness = 1.0f;        // Overall shadow softness
        float m_blockerSearchRadius = 0.005f; // Search radius for blocker search phase
        float m_lightSize = 0.001f;           // Size of the virtual light (smaller = sharper shadows)
        uint16 m_shadowMapSize = 2048;        // Size of the shadow map texture (increased for quality)
    };
}
