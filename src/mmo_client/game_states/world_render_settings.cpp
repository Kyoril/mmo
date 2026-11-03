// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_render_settings.h"

#include "console/console_var.h"
#include "deferred_shading/deferred_renderer.h"
#include "graphics/graphics_device.h"
#include "log/default_log_levels.h"
#include "math/clamp.h"
#include "scene_graph/foliage_chunk.h"
#include "scene_graph/render_queue.h"
#include "ui/world_frame.h"
#include "ui/world_renderer.h"

#include <algorithm>

namespace mmo
{
	namespace
	{
		DeferredRenderer *FindWorldDeferredRenderer()
		{
			const WorldFrame *worldFrame = WorldFrame::GetWorldFrame();
			if (!worldFrame)
			{
				return nullptr;
			}

			const WorldRenderer *renderer = reinterpret_cast<const WorldRenderer *>(worldFrame->GetRenderer());
			if (!renderer)
			{
				return nullptr;
			}

			return renderer->GetDeferredRenderer();
		}
	}

	void WorldRenderSettings::Register()
	{
		m_depthBias = Add("ShadowDepthBias", "", "138.6", &WorldRenderSettings::ApplyShadowBias);
		m_slopeDepthBias = Add("ShadowSlopeBias", "", "0.1", &WorldRenderSettings::ApplyShadowBias);
		m_clampDepthBias = Add("ShadowClampBias", "", "0.0", &WorldRenderSettings::ApplyShadowBias);

		m_shadowTextureSize = Add("ShadowTextureSize", "Shadow map resolution per cascade: 0 = 512, 1 = 1024, 2 = 2048, 3 = 4096. Higher values sharpen shadows at the cost of GPU memory and shadow-pass fill rate.", "2", &WorldRenderSettings::ApplyShadowTextureSize);
		m_shadowQuality = Add("ShadowQuality", "Shadow detail preset: 0 = Low (2 cascades, 4 PCF taps), 1 = Medium (3/8), 2 = High (4/16). Lower values improve performance.", "2", &WorldRenderSettings::ApplyShadowQuality);
		m_shadowDistance = Add("gxShadowDistance", "How far sun shadows reach from the camera in metres (50 to 1000). Shorter is faster in forests and sharpens nearby shadows.", "250", &WorldRenderSettings::ApplyShadowQuality);
		m_shadowTemporal = Add("ShadowTemporal", "Temporal cascade staggering: 1 = distant cascades refresh every few frames (faster), 0 = every cascade every frame.", "1", &WorldRenderSettings::ApplyShadowTemporal);
		m_shadowLightStep = Add("ShadowLightStep", "Sun-shadow direction quantization step in degrees. Shadows hold perfectly still until the day/night sun has moved this far, then jump once; prevents distant shadow flicker. 0 = track the sun continuously.", "0.35", &WorldRenderSettings::ApplyShadowLightStep);

		// Internal 3D render-scale: the world is rendered at this fraction of the frame size and then
		// upscaled. 1.0 = native; lower values trade sharpness for a large performance gain on weak
		// GPUs. Read directly by WorldRenderer each frame, so no change handler is required here.
		Add("gxRenderScale", "3D render resolution scale (0.25 to 1.0). Lower values improve performance by rendering the world at a lower resolution and upscaling.", "1.0");

		m_depthPrepass = Add("gxDepthPrepass", "Render an opaque depth pre-pass before the G-Buffer pass. Reduces overdraw shading cost in scenes with heavy opaque overdraw (e.g. dense foliage). 1 = on, 0 = off.", "1", &WorldRenderSettings::ApplyDepthPrepass);

		// Screen-space ambient occlusion (visibility-bitmask / SSILVB occlusion core). All
		// distances are world units; 1 unit = 1 metre.
		m_ssao = Add("gxSsao", "Screen-space ambient occlusion. 1 = on, 0 = off.", "1", &WorldRenderSettings::ApplySsaoEnabled);
		m_ssaoQuality = Add("gxSsaoQuality", "SSAO detail preset: 0 = Low (2 slices, 4 steps), 1 = Medium (3/8), 2 = High (4/12). Lower values improve performance.", "2", &WorldRenderSettings::ApplySsaoQuality);
		m_ssaoHalfRes = Add("gxSsaoHalfRes", "Compute SSAO at half resolution and upscale. 1 = on (faster), 0 = full resolution (sharper).", "1", &WorldRenderSettings::ApplySsaoHalfRes);
		m_ssaoRadius = Add("gxSsaoRadius", "SSAO sampling radius in world units (metres). Larger values need a higher gxSsaoQuality to avoid banding.", "0.75", &WorldRenderSettings::ApplySsaoParameters);
		m_ssaoIntensity = Add("gxSsaoIntensity", "SSAO strength multiplier. Higher values darken occluded areas more.", "1.0", &WorldRenderSettings::ApplySsaoParameters);
		m_ssaoThickness = Add("gxSsaoThickness", "Assumed occluder thickness in world units (metres). Too low leaks light through solid geometry; too high makes thin objects occlude like walls.", "0.25", &WorldRenderSettings::ApplySsaoParameters);
		m_ssaoDebug = Add("gxSsaoDebug", "Visualize the raw SSAO term instead of the lit scene. 1 = on, 0 = off.", "0", &WorldRenderSettings::ApplySsaoDebug);

		// Screen-space contact shadows, marched inline in the deferred lighting pass. All distances
		// are world units; 1 unit = 1 metre.
		m_contactShadows = Add("gxContactShadows", "Screen-space contact shadows for the sun. Adds the short, sharp shadows where objects meet surfaces that shadow maps are too coarse to resolve. 1 = on, 0 = off.", "1", &WorldRenderSettings::ApplyContactShadowsEnabled);
		m_contactShadowQuality = Add("gxContactShadowQuality", "Contact shadow detail preset: 0 = Low (4 steps), 1 = Medium (8), 2 = High (16). Lower values improve performance.", "1", &WorldRenderSettings::ApplyContactShadowQuality);
		m_contactShadowLength = Add("gxContactShadowLength", "Contact shadow ray length in world units (metres). Longer rays catch more distant contacts but cost more and grow noisier at a given gxContactShadowQuality.", "0.3", &WorldRenderSettings::ApplyContactShadowParameters);
		m_contactShadowThickness = Add("gxContactShadowThickness", "Assumed occluder thickness in world units (metres). Too low and solid objects stop occluding; too high and distant background geometry shadows the foreground.", "0.2", &WorldRenderSettings::ApplyContactShadowParameters);
		m_contactShadowIntensity = Add("gxContactShadowIntensity", "Contact shadow strength, from 0 to 1. 1 = a hit shadows fully.", "1.0", &WorldRenderSettings::ApplyContactShadowParameters);
		m_contactShadowFade = Add("gxContactShadowFade", "Distance in world units (metres) at which contact shadows finish fading out. They are unreliable beyond this because the G-Buffer stores depth at half-float precision.", "35", &WorldRenderSettings::ApplyContactShadowParameters);
		m_contactShadowBias = Add("gxContactShadowBias", "Contact shadow normal bias in world units (metres). Raise if surfaces shadow themselves, which shows up as a grey wash or moire under gxContactShadowDebug.", "0.02", &WorldRenderSettings::ApplyContactShadowParameters);
		m_contactShadowDebug = Add("gxContactShadowDebug", "Visualize the raw contact shadow term instead of the lit scene. Healthy is near-white with thin dark bands at contacts. 1 = on, 0 = off.", "0", &WorldRenderSettings::ApplyContactShadowDebug);

		// Height fog, light shafts and bloom quality. The look itself (fog, shafts, exposure, bloom
		// strength) comes from the zone's environment profile.
		m_atmosphereQuality = Add("gxAtmosphereQuality", "Volumetric fog quality: 0 = Off (smooth height fog only), 1 = Low (24 px cells, 32 slices), 2 = Medium (16 px, 48), 3 = High (12 px, 64), 4 = Ultra (8 px, 96).", "3", &WorldRenderSettings::ApplyAtmosphere);
		m_volumetricFogRange = Add("gxVolumetricFogRange", "How many metres in front of the camera the volumetric fog covers (50 to 300, the shadow range). Fog beyond uses a smooth closed-form estimate.", "200", &WorldRenderSettings::ApplyAtmosphere);
		m_volumetricFogHistory = Add("gxVolumetricFogHistory", "How much of the previous frame the volumetric fog keeps (0 to 0.95). Higher values smooth out sampling noise and aliasing; lower values react faster and stay sharper while the camera moves. 0 (default) disables the history: the fog is then a pure function of the camera pose.", "0", &WorldRenderSettings::ApplyAtmosphere);
		m_fogSunMode = Add("gxFogSunMode", "Volumetric fog sun filtering: 0 box filter (fixed taps, no history), 1 temporal (one blue-noise jittered tap, clamped history, fog shadow map), 2 temporal with the surface cascades.", "0", &WorldRenderSettings::ApplyAtmosphere);
		m_atmosphereDebug = Add("gxAtmosphereDebug", "Fog debug view: 0 = off, 1 = scattered light, 2 = transmittance, 3 = fog density, 4 = lights per fog block.", "0", &WorldRenderSettings::ApplyAtmosphere);

		m_bloomQuality = Add("gxBloomQuality", "Bloom quality: 0 = Off, 1 = Low (quarter resolution, 4 levels), 2 = High (half resolution, 6 levels).", "2", &WorldRenderSettings::ApplyBloom);

		// Read every frame by WorldState::ApplyEnvironmentToRenderer through GetExposure.
		m_exposure = Add("gxExposure", "Player brightness: multiplies the zone's environment exposure before tone mapping (0.5 to 2 in Options).", "1.0");

		// Profiling aids: hide whole kinds of objects from the camera view or from the shadow maps to
		// measure what they cost (compare benchmark runs with and without).
		ConsoleVar *hideViewTypesVar = Add("gxDebugHideViewTypes", "Debug: comma separated movable types not rendered in the camera view (Tile, TerrainBatch, FarPage, FoliageChunk, InstancedFoliageChunk, Entity, WorldModelInstance, WorldModelBatch).", "");
		ConsoleVar *hideShadowTypesVar = Add("gxDebugHideShadowTypes", "Debug: comma separated movable types not rendered into shadow maps (see gxDebugHideViewTypes).", "");
		m_connections += hideViewTypesVar->Changed.connect([](ConsoleVar &var, const std::string &) { RenderDebugFilter::hiddenInView = RenderDebugFilter::Parse(var.GetStringValue()); });
		m_connections += hideShadowTypesVar->Changed.connect([](ConsoleVar &var, const std::string &) { RenderDebugFilter::hiddenInShadows = RenderDebugFilter::Parse(var.GetStringValue()); });
		RenderDebugFilter::hiddenInView = RenderDebugFilter::Parse(hideViewTypesVar->GetStringValue());
		RenderDebugFilter::hiddenInShadows = RenderDebugFilter::Parse(hideShadowTypesVar->GetStringValue());

		ConsoleVar *foliageCullingVar = Add("gxFoliageInstanceCulling", "Cull grass and tree instances individually per render pass instead of drawing whole foliage cells. 1 = on (faster), 0 = off.", "1");
		m_connections += foliageCullingVar->Changed.connect([](ConsoleVar &var, const std::string &) { FoliageChunk::SetPerInstanceCullingEnabled(var.GetBoolValue()); });
		FoliageChunk::SetPerInstanceCullingEnabled(foliageCullingVar->GetBoolValue());

		ConsoleVar *shadowAlphaTestVar = Add("gxShadowAlphaTest", "Alpha test cut-out materials (leaves, grass) in shadow maps. 0 renders them as solid shapes: much faster in forests, but leaves cast blocky shadows.", "1");
		m_connections += shadowAlphaTestVar->Changed.connect([](ConsoleVar &var, const std::string &) { GraphicsDevice::Get().SetAlphaTestedShadows(var.GetBoolValue()); });
		GraphicsDevice::Get().SetAlphaTestedShadows(shadowAlphaTestVar->GetBoolValue());

		ApplyAll();
	}

	void WorldRenderSettings::Unregister()
	{
		m_connections.disconnect();

		for (const char *name : m_names)
		{
			ConsoleVarMgr::UnregisterConsoleVar(name);
		}

		m_names.clear();
		m_applyFunctions.clear();
		m_exposure = nullptr;
	}

	void WorldRenderSettings::ApplyAll() const
	{
		DeferredRenderer *renderer = FindWorldDeferredRenderer();
		if (!renderer)
		{
			return;
		}

		for (const ApplyFunction apply : m_applyFunctions)
		{
			(this->*apply)(*renderer);
		}
	}

	float WorldRenderSettings::GetExposure() const
	{
		return m_exposure ? m_exposure->GetFloatValue() : 1.0f;
	}

	ConsoleVar *WorldRenderSettings::Add(const char *name, const char *description, const char *defaultValue, const ApplyFunction apply)
	{
		ConsoleVar *var = ConsoleVarMgr::RegisterConsoleVar(name, description, defaultValue);
		m_names.push_back(name);

		if (apply)
		{
			m_connections += var->Changed.connect([this, apply](ConsoleVar &, const std::string &)
			{
				if (DeferredRenderer *renderer = FindWorldDeferredRenderer())
				{
					(this->*apply)(*renderer);
				}
			});

			if (std::find(m_applyFunctions.begin(), m_applyFunctions.end(), apply) == m_applyFunctions.end())
			{
				m_applyFunctions.push_back(apply);
			}
		}

		return var;
	}

	void WorldRenderSettings::ApplyShadowBias(DeferredRenderer &renderer) const
	{
		DLOG("Updating depth bias values");
		renderer.SetDepthBias(m_depthBias->GetFloatValue(), m_slopeDepthBias->GetFloatValue(), m_clampDepthBias->GetFloatValue());
	}

	void WorldRenderSettings::ApplyShadowTextureSize(DeferredRenderer &renderer) const
	{
		const uint16 shadowTexSizes[] = { 512, 1024, 2048, 4096 };

		const uint16 shadowTextureSize = shadowTexSizes[Clamp(m_shadowTextureSize->GetIntValue(), 0, 3)];
		ILOG("Updating shadow texture size to " << shadowTextureSize << "x" << shadowTextureSize);
		renderer.SetShadowMapSize(shadowTextureSize);
	}

	void WorldRenderSettings::ApplyShadowQuality(DeferredRenderer &renderer) const
	{
		const int level = Clamp(m_shadowQuality->GetIntValue(), 0, 2);
		ILOG("Updating shadow quality to level " << level);
		renderer.SetShadowQuality(level);
		renderer.SetShadowDistance(m_shadowDistance->GetFloatValue());
	}

	void WorldRenderSettings::ApplyShadowTemporal(DeferredRenderer &renderer) const
	{
		const bool enabled = m_shadowTemporal->GetIntValue() != 0;
		ILOG("Temporal shadow staggering " << (enabled ? "enabled" : "disabled"));
		renderer.SetTemporalShadowsEnabled(enabled);
	}

	void WorldRenderSettings::ApplyShadowLightStep(DeferredRenderer &renderer) const
	{
		const float degrees = m_shadowLightStep->GetFloatValue();
		ILOG("Shadow light direction step set to " << degrees << " degrees");
		renderer.SetShadowLightStepDegrees(degrees);
	}

	void WorldRenderSettings::ApplyDepthPrepass(DeferredRenderer &renderer) const
	{
		const bool enabled = m_depthPrepass->GetIntValue() != 0;
		ILOG("Depth pre-pass " << (enabled ? "enabled" : "disabled"));
		renderer.SetDepthPrepassEnabled(enabled);
	}

	void WorldRenderSettings::ApplySsaoEnabled(DeferredRenderer &renderer) const
	{
		const bool enabled = m_ssao->GetIntValue() != 0;
		ILOG("SSAO " << (enabled ? "enabled" : "disabled"));
		renderer.SetSsaoEnabled(enabled);
	}

	void WorldRenderSettings::ApplySsaoQuality(DeferredRenderer &renderer) const
	{
		const int level = Clamp(m_ssaoQuality->GetIntValue(), 0, 2);
		ILOG("Updating SSAO quality to level " << level);
		renderer.SetSsaoQuality(level);
	}

	void WorldRenderSettings::ApplySsaoHalfRes(DeferredRenderer &renderer) const
	{
		const bool enabled = m_ssaoHalfRes->GetIntValue() != 0;
		ILOG("SSAO half resolution " << (enabled ? "enabled" : "disabled"));
		renderer.SetSsaoHalfResolution(enabled);
	}

	void WorldRenderSettings::ApplySsaoParameters(DeferredRenderer &renderer) const
	{
		// Each setter clamps its own value, so pushing all three on any change is both cheap and
		// idempotent.
		renderer.SetSsaoRadius(m_ssaoRadius->GetFloatValue());
		renderer.SetSsaoIntensity(m_ssaoIntensity->GetFloatValue());
		renderer.SetSsaoThickness(m_ssaoThickness->GetFloatValue());

		DLOG("Updated SSAO parameters");
	}

	void WorldRenderSettings::ApplySsaoDebug(DeferredRenderer &renderer) const
	{
		const bool enabled = m_ssaoDebug->GetIntValue() != 0;
		ILOG("SSAO debug visualization " << (enabled ? "enabled" : "disabled"));
		renderer.SetSsaoDebugVisualization(enabled);
	}

	void WorldRenderSettings::ApplyContactShadowsEnabled(DeferredRenderer &renderer) const
	{
		const bool enabled = m_contactShadows->GetIntValue() != 0;
		ILOG("Contact shadows " << (enabled ? "enabled" : "disabled"));
		renderer.SetContactShadowsEnabled(enabled);
	}

	void WorldRenderSettings::ApplyContactShadowQuality(DeferredRenderer &renderer) const
	{
		const int level = Clamp(m_contactShadowQuality->GetIntValue(), 0, 2);
		ILOG("Updating contact shadow quality to level " << level);
		renderer.SetContactShadowQuality(level);
	}

	void WorldRenderSettings::ApplyContactShadowParameters(DeferredRenderer &renderer) const
	{
		// Each setter clamps its own value, so pushing all five on any change is both cheap and
		// idempotent.
		renderer.SetContactShadowRayLength(m_contactShadowLength->GetFloatValue());
		renderer.SetContactShadowThickness(m_contactShadowThickness->GetFloatValue());
		renderer.SetContactShadowIntensity(m_contactShadowIntensity->GetFloatValue());
		renderer.SetContactShadowFadeDistance(m_contactShadowFade->GetFloatValue());
		renderer.SetContactShadowNormalBias(m_contactShadowBias->GetFloatValue());

		DLOG("Updated contact shadow parameters");
	}

	void WorldRenderSettings::ApplyContactShadowDebug(DeferredRenderer &renderer) const
	{
		const bool enabled = m_contactShadowDebug->GetIntValue() != 0;
		ILOG("Contact shadow debug visualization " << (enabled ? "enabled" : "disabled"));
		renderer.SetContactShadowDebugVisualization(enabled);
	}

	void WorldRenderSettings::ApplyAtmosphere(DeferredRenderer &renderer) const
	{
		renderer.SetAtmosphereQuality(m_atmosphereQuality->GetIntValue());
		renderer.SetVolumetricFogRange(m_volumetricFogRange->GetFloatValue());
		renderer.SetVolumetricFogHistory(m_volumetricFogHistory->GetFloatValue());
		renderer.SetAtmosphereDebugMode(m_atmosphereDebug->GetIntValue());
		renderer.GetVolumetricFogSettings().SetSunMode(m_fogSunMode->GetIntValue());
	}

	void WorldRenderSettings::ApplyBloom(DeferredRenderer &renderer) const
	{
		renderer.SetBloomQuality(m_bloomQuality->GetIntValue());
	}
}
