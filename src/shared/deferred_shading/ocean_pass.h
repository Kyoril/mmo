// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "ocean_settings.h"

#include "base/non_copyable.h"
#include "graphics/graphics_device.h"

#include <array>
#include <chrono>
#include <vector>

namespace mmo
{
	class Camera;
	class Texture;

	/// @brief The FFT ocean: simulates the sea surface on the GPU and draws it, from the camera out to
	///        the horizon, over the lit opaque scene.
	///
	/// Simulation (compute, every frame): OceanCascadeCount cascades of OceanFftResolution^2 waves.
	/// CS_OceanSpectrum builds h0(k) when the wave settings change; CS_OceanEvolve advances it to the
	/// current time; CS_OceanFft transforms rows then columns; CS_OceanAssemble writes the
	/// displacement, the mipmapped slope/foam texture and the persistent foam.
	///
	/// Surface (VS_Ocean/PS_Ocean): a camera-centred polar grid displaced by the cascades, clipped to
	/// where the terrain says the sea is (the ocean mask), shaded with refraction, absorption,
	/// subsurface scattering, reflections, sun glints and foam, then fogged like the opaque scene.
	///
	/// D3D11 only: on a device without compute or texture arrays IsSupported() is false and the
	/// terrain keeps meshing ocean water itself.
	class OceanPass final : public NonCopyable
	{
	public:
		explicit OceanPass(GraphicsDevice& device);
		~OceanPass() override = default;

	public:
		/// @brief Whether the device can run the ocean at all.
		[[nodiscard]] bool IsSupported() const;

		/// @brief Whether the ocean will be simulated and drawn this frame.
		[[nodiscard]] bool IsActive() const { return IsSupported() && m_surface.enabled; }

		/// @brief Replaces the wave and shading settings. Rebuilds the spectrum only if the waves changed.
		void SetSettings(const OceanSettings& settings);

		/// @brief The current wave and shading settings.
		[[nodiscard]] const OceanSettings& GetSettings() const { return m_settings; }

		/// @brief Sets where the sea is: enable flag, sea level and the mask window placement.
		void SetSurfaceState(const OceanSurfaceState& state);

		/// @brief The current surface state.
		[[nodiscard]] const OceanSurfaceState& GetSurfaceState() const { return m_surface; }

		/// @brief Uploads the ocean mask: texelsPerSide^2 texels of four bytes (R = ocean present,
		///        G = water depth / OceanMaskDepthRange, B = shore distance / ocean_shore::DistanceRange,
		///        see ocean_shore::BuildShaderMask). An empty buffer disables masking (open sea
		///        everywhere).
		void UploadMask(const std::vector<uint8>& texels, uint32 texelsPerSide);

		/// @brief Advances the simulation to the current wall-clock time. Call once per frame before
		///        Render, while no scene targets are bound to the compute stage.
		void Simulate();

		/// @brief Records the sky drawn on screen into the map the surface reflects where the
		///        reflected ray leaves the screen. Call after the sky and fog are composited into
		///        sceneColor and before Render.
		/// @param camera The camera the scene was drawn with.
		/// @param sceneColor Copy of the lit, fogged scene colour (linear HDR).
		/// @param sceneDepth G-buffer target with radial depth in alpha (0 = sky).
		void CaptureSky(Camera& camera, Texture& sceneColor, Texture& sceneDepth);

		/// @brief Draws the surface into the currently bound HDR colour target and scene depth.
		/// @param camera The camera to draw for.
		/// @param cameraBuffer The scene's camera constant buffer (b1).
		/// @param exposure Exposure of the tone mapper, to bring display-referred sky colours into the
		///        linear HDR scene the same way forward materials do.
		void Render(Camera& camera, ConstantBuffer& cameraBuffer, float exposure);

	private:
		void CreateResources();

		void BuildSpectrum();

		void UpdateSimulationConstants(uint32 fftPass);

	private:
		GraphicsDevice& m_device;
		OceanSettings m_settings;
		OceanSurfaceState m_surface;

		ShaderPtr m_spectrumCs;
		ShaderPtr m_evolveCs;
		ShaderPtr m_fftCs;
		ShaderPtr m_assembleCs;
		ShaderPtr m_skyCaptureCs;
		ShaderPtr m_surfaceVs;
		ShaderPtr m_surfacePs;

		ConstantBufferPtr m_simulationBuffer;
		ConstantBufferPtr m_renderBuffer;
		ConstantBufferPtr m_skyCaptureBuffer;

		TextureArrayPtr m_spectrum;
		TextureArrayPtr m_fftPing;
		TextureArrayPtr m_fftPong;
		TextureArrayPtr m_displacement;
		TextureArrayPtr m_surfaceTexture;
		TextureArrayPtr m_foamState;
		TextureArrayPtr m_mask;
		/// Sky as drawn, by azimuth and elevation (CS_OceanSkyCapture). Ping-pong, since typed UAV
		/// loads of RGBA16F are not available everywhere.
		std::array<TextureArrayPtr, 2> m_skyMaps;
		uint32 m_skyMapIndex { 0 };
		uint32 m_maskTexelsPerSide { 0 };

		SamplerStatePtr m_wrapSampler;
		SamplerStatePtr m_clampSampler;

		VertexBufferPtr m_gridVertices;
		IndexBufferPtr m_gridIndices;
		uint32 m_gridIndexCount { 0 };
		float m_gridAngularStep { 0.0f };

		bool m_spectrumDirty { true };
		double m_time { 0.0 };
		float m_deltaTime { 0.0f };
		bool m_hasLastTick { false };
		std::chrono::steady_clock::time_point m_lastTick;
	};
}
