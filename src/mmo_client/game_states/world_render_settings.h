// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/signal.h"
#include "base/typedefs.h"

#include <vector>

namespace mmo
{
	class ConsoleVar;
	class DeferredRenderer;

	/// @brief Owns the console variables that configure the world's deferred renderer (shadows,
	///        SSAO, contact shadows, volumetric fog, bloom, exposure, render scale and the render
	///        debug filters) and pushes their values to the renderer whenever they change.
	///
	/// Settings that configure world objects rather than the renderer (foliage, terrain, water,
	/// the sun light) stay with the objects that own them.
	class WorldRenderSettings final : public NonCopyable
	{
	public:
		/// @brief Registers all console variables and applies their current values.
		void Register();

		/// @brief Unregisters all console variables and disconnects their change handlers.
		void Unregister();

		/// @brief Pushes every setting to the current world renderer. Does nothing while not
		///        registered or while there is no world renderer.
		void ApplyAll() const;

		/// @brief Gets the player's brightness multiplier (gxExposure), or 1 while not registered.
		[[nodiscard]] float GetExposure() const;

	private:
		/// Pushes one group of settings to the renderer.
		typedef void (WorldRenderSettings::*ApplyFunction)(DeferredRenderer &renderer) const;

		/// @brief Registers a console variable and remembers it for unregistration. If apply is
		///        set, it runs on the world renderer whenever the variable changes.
		ConsoleVar *Add(const char *name, const char *description, const char *defaultValue, ApplyFunction apply = nullptr);

		void ApplyShadowBias(DeferredRenderer &renderer) const;

		void ApplyShadowTextureSize(DeferredRenderer &renderer) const;

		/// @brief Applies ShadowQuality together with gxShadowDistance.
		void ApplyShadowQuality(DeferredRenderer &renderer) const;

		void ApplyShadowTemporal(DeferredRenderer &renderer) const;

		void ApplyShadowLightStep(DeferredRenderer &renderer) const;

		void ApplyDepthPrepass(DeferredRenderer &renderer) const;

		void ApplySsaoEnabled(DeferredRenderer &renderer) const;

		void ApplySsaoQuality(DeferredRenderer &renderer) const;

		void ApplySsaoHalfRes(DeferredRenderer &renderer) const;

		/// @brief Applies gxSsaoRadius, gxSsaoIntensity and gxSsaoThickness together.
		void ApplySsaoParameters(DeferredRenderer &renderer) const;

		void ApplySsaoDebug(DeferredRenderer &renderer) const;

		void ApplyContactShadowsEnabled(DeferredRenderer &renderer) const;

		void ApplyContactShadowQuality(DeferredRenderer &renderer) const;

		/// @brief Applies the contact shadow length, thickness, intensity, fade and bias together.
		void ApplyContactShadowParameters(DeferredRenderer &renderer) const;

		void ApplyContactShadowDebug(DeferredRenderer &renderer) const;

		/// @brief Applies the volumetric fog quality, range, history, sun mode and debug view together.
		void ApplyAtmosphere(DeferredRenderer &renderer) const;

		void ApplyBloom(DeferredRenderer &renderer) const;

	private:
		/// Console variable names registered by Register, for Unregister.
		std::vector<const char *> m_names;

		/// Distinct apply functions in registration order, for ApplyAll.
		std::vector<ApplyFunction> m_applyFunctions;

		scoped_connection_container m_connections;

		ConsoleVar *m_depthBias = nullptr;
		ConsoleVar *m_slopeDepthBias = nullptr;
		ConsoleVar *m_clampDepthBias = nullptr;
		ConsoleVar *m_shadowTextureSize = nullptr;
		ConsoleVar *m_shadowQuality = nullptr;
		ConsoleVar *m_shadowDistance = nullptr;
		ConsoleVar *m_shadowTemporal = nullptr;
		ConsoleVar *m_shadowLightStep = nullptr;
		ConsoleVar *m_depthPrepass = nullptr;

		ConsoleVar *m_ssao = nullptr;
		ConsoleVar *m_ssaoQuality = nullptr;
		ConsoleVar *m_ssaoHalfRes = nullptr;
		ConsoleVar *m_ssaoRadius = nullptr;
		ConsoleVar *m_ssaoIntensity = nullptr;
		ConsoleVar *m_ssaoThickness = nullptr;
		ConsoleVar *m_ssaoDebug = nullptr;

		ConsoleVar *m_contactShadows = nullptr;
		ConsoleVar *m_contactShadowQuality = nullptr;
		ConsoleVar *m_contactShadowLength = nullptr;
		ConsoleVar *m_contactShadowThickness = nullptr;
		ConsoleVar *m_contactShadowIntensity = nullptr;
		ConsoleVar *m_contactShadowFade = nullptr;
		ConsoleVar *m_contactShadowBias = nullptr;
		ConsoleVar *m_contactShadowDebug = nullptr;

		ConsoleVar *m_atmosphereQuality = nullptr;
		ConsoleVar *m_volumetricFogRange = nullptr;
		ConsoleVar *m_volumetricFogHistory = nullptr;
		ConsoleVar *m_fogSunMode = nullptr;
		ConsoleVar *m_atmosphereDebug = nullptr;
		ConsoleVar *m_bloomQuality = nullptr;
		ConsoleVar *m_exposure = nullptr;
	};
}
