// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "render_target.h"
#include "render_texture.h"

namespace mmo
{
    /// @brief A G-Buffer for deferred rendering.
    class GBuffer : public RenderTarget
    {
    public:
        /// @brief Creates a new G-Buffer.
        /// @param width Width of the G-Buffer.
        /// @param height Height of the G-Buffer.
        GBuffer(uint16 width, uint16 height);

        /// @brief Destructor.
        ~GBuffer() override = default;

    public:
        /// @brief Activates the G-Buffer for rendering.
        void Activate() override;

        /// @brief Clears the G-Buffer.
        /// @param flags Clear flags.
        void Clear(ClearFlags flags) override;

        /// @brief Updates the G-Buffer.
        void Update() override;

        /// @brief Resizes the G-Buffer.
        /// @param width New width.
        /// @param height New height.
        void Resize(uint16 width, uint16 height) override;

        /// @brief Binds the G-Buffer textures for reading in the lighting pass.
        void BindForReading();

        /// @brief Gets the albedo render texture.
        /// @return The albedo render texture.
        RenderTexturePtr GetAlbedoTexture() const { return m_albedoRT; }

        /// @brief Gets the normal render texture.
        /// @return The normal render texture.
        RenderTexturePtr GetNormalTexture() const { return m_normalRT; }

        /// @brief Gets the material properties render texture.
        /// @return The material properties render texture.
        RenderTexturePtr GetMaterialPropertiesTexture() const { return m_materialPropertiesRT; }

        /// @brief Gets the emissive render texture.
        /// @return The emissive render texture.
        RenderTexturePtr GetEmissiveTexture() const { return m_emissiveRT; }

        /// @brief Gets the depth render texture.
        /// @return The depth render texture.
        RenderTexturePtr GetDepthTexture() const { return m_depthRT; }

    private:
        /// @brief Initializes the G-Buffer.
        void Initialize();

    private:
        RenderTexturePtr m_albedoRT;              // RGB: Albedo, A: Opacity Mask
        RenderTexturePtr m_normalRT;              // RGB: Normal
        RenderTexturePtr m_materialPropertiesRT;  // R: Metalness, G: Roughness, B: Specularity, A: AO
        RenderTexturePtr m_emissiveRT;            // RGB: Emissive
        RenderTexturePtr m_depthRT;               // R: Depth
    };

    typedef std::shared_ptr<GBuffer> GBufferPtr;
}
