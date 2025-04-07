// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "shared/graphics/g_buffer.h"
#include "shared/scene_graph/scene.h"
#include "shared/graphics/vertex_buffer.h"
#include "shared/graphics/pixel_shader.h"
#include "shared/graphics/vertex_shader.h"
#include "shared/graphics/constant_buffer.h"
#include "math/vector2.h"
#include "math/vector3.h"
#include "math/vector4.h"

#include <fstream>
#include <vector>

namespace mmo
{
    /// @brief Class that manages deferred rendering.
    class DeferredRenderer
    {
    public:
        /// @brief Creates a new deferred renderer.
        /// @param width Width of the render target.
        /// @param height Height of the render target.
        DeferredRenderer(uint32 width, uint32 height);

        /// @brief Destructor.
        ~DeferredRenderer() = default;

    public:
        /// @brief Renders a scene using deferred shading.
        /// @param scene The scene to render.
        /// @param camera The camera to use for rendering.
        void Render(Scene& scene, Camera& camera);

        /// @brief Gets the final render target.
        /// @return The final render target.
        RenderTexturePtr GetFinalRenderTarget() const { return m_finalRenderTarget; }

        /// @brief Resizes the renderer.
        /// @param width New width.
        /// @param height New height.
        void Resize(uint32 width, uint32 height);

    private:
        /// @brief Initializes the renderer.
        void Initialize();

        /// @brief Renders the geometry pass.
        /// @param scene The scene to render.
        /// @param camera The camera to use for rendering.
        void RenderGeometryPass(Scene& scene, Camera& camera);

        /// @brief Renders the lighting pass.
        /// @param scene The scene to render.
        /// @param camera The camera to use for rendering.
        void RenderLightingPass(Scene& scene, Camera& camera);

        /// @brief Renders the final composition pass.
        void RenderCompositionPass();

        /// @brief Loads a shader from a file.
        /// @param filename The filename of the shader.
        /// @param type The type of shader.
        /// @param outCode The output buffer for the shader code.
        /// @return True if the shader was loaded successfully, false otherwise.
        bool LoadShaderFromFile(const std::string& filename, ShaderType type, std::vector<uint8>& outCode);

    private:
        // Light buffer structure for the lighting pass
        struct Light
        {
            Vector3 Position;    // For point and spot lights
            float Range;         // For point and spot lights
            Vector3 Direction;   // For directional and spot lights
            float SpotAngle;     // For spot lights
            Vector4 Color;       // RGB: Color, A: Intensity
            uint32 Type;         // 0: Directional, 1: Point, 2: Spot
            uint32 CastsShadow;  // 0: No, 1: Yes
            Vector2 Padding;     // For 16-byte alignment
        };

        struct LightBuffer
        {
            uint32 LightCount;
            Vector3 AmbientColor;
            Light Lights[16];    // Maximum 16 lights
        };

    private:
        uint32 m_width;
        uint32 m_height;
        GBufferPtr m_gBuffer;
        RenderTexturePtr m_finalRenderTarget;
        VertexBufferPtr m_fullScreenQuadBuffer;
        ShaderPtr m_lightingVertexShader;
        ShaderPtr m_lightingPixelShader;
        ConstantBufferPtr m_lightBuffer;
    };
}
