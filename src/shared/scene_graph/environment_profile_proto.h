// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "log/default_log_levels.h"
#include "scene_graph/environment_profile.h"

#include <algorithm>
#include <map>
#include <memory>

namespace mmo
{
	// Templates rather than functions: the editor works on mmo::proto types and the client on
	// mmo::proto_client types. The generated classes mirror each other field for field, so one
	// template serves both without this header depending on either protobuf library.

	/// @brief Converts an authored curve record into a runtime curve.
	/// @tparam TCurveData proto::ColorCurveData or proto_client::ColorCurveData.
	/// @param data The authored keys.
	/// @param outCurve Receives the curve. Left untouched when the record has no keys.
	/// @return True when the record had keys and replaced outCurve.
	/// @remark User tangents are only honoured with exactly 4 in and 4 out values; anything else
	///         logs a warning and falls back to auto tangents.
	template <typename TCurveData>
	bool LoadColorCurve(const TCurveData& data, ColorCurve& outCurve)
	{
		if (data.key_size() == 0)
		{
			return false;
		}

		ColorCurve curve;
		curve.Clear();

		for (const auto& key : data.key())
		{
			ColorKey colorKey(key.time(), Vector4(key.r(), key.g(), key.b(), key.a()));

			if (key.tangent_mode() != 0)
			{
				if (key.in_tangent_size() == 4 && key.out_tangent_size() == 4)
				{
					colorKey.inTangent = Vector4(key.in_tangent(0), key.in_tangent(1), key.in_tangent(2), key.in_tangent(3));
					colorKey.outTangent = Vector4(key.out_tangent(0), key.out_tangent(1), key.out_tangent(2), key.out_tangent(3));
					colorKey.tangentMode = static_cast<uint8>(key.tangent_mode());
				}
				else
				{
					WLOG("Environment curve key at time " << key.time() << " has malformed user tangents; using auto tangents");
				}
			}

			curve.AddKey(colorKey);
		}

		curve.CalculateTangents();
		outCurve = std::move(curve);
		return true;
	}

	/// @brief Writes a runtime curve into an authored curve record, replacing its keys.
	/// @remark Tangents are stored only for user-tangent keys; auto tangents are recomputed on load.
	template <typename TCurveData>
	void StoreColorCurve(const ColorCurve& curve, TCurveData& outData)
	{
		outData.clear_key();

		for (const ColorKey& colorKey : curve.GetKeys())
		{
			auto* key = outData.add_key();
			key->set_time(colorKey.time);
			key->set_r(colorKey.color.x);
			key->set_g(colorKey.color.y);
			key->set_b(colorKey.color.z);
			key->set_a(colorKey.color.w);

			if (colorKey.tangentMode != 0)
			{
				key->set_tangent_mode(colorKey.tangentMode);
				key->add_in_tangent(colorKey.inTangent.x);
				key->add_in_tangent(colorKey.inTangent.y);
				key->add_in_tangent(colorKey.inTangent.z);
				key->add_in_tangent(colorKey.inTangent.w);
				key->add_out_tangent(colorKey.outTangent.x);
				key->add_out_tangent(colorKey.outTangent.y);
				key->add_out_tangent(colorKey.outTangent.z);
				key->add_out_tangent(colorKey.outTangent.w);
			}
		}
	}

	/// @brief Converts an authored profile into a runtime profile.
	/// @tparam TProfile proto::EnvironmentProfile or proto_client::EnvironmentProfile.
	/// @return The profile. Unauthored curves come from the Default; fixed values are clamped.
	template <typename TProfile>
	[[nodiscard]] EnvironmentProfile LoadEnvironmentProfile(const TProfile& profile)
	{
		EnvironmentProfile result = EnvironmentProfile::MakeDefault();

		LoadColorCurve(profile.sky_horizon(), result.skyHorizon);
		LoadColorCurve(profile.sky_zenith(), result.skyZenith);
		LoadColorCurve(profile.clouds(), result.clouds);
		LoadColorCurve(profile.ambient(), result.ambient);
		LoadColorCurve(profile.sun(), result.sun);
		LoadColorCurve(profile.moon(), result.moon);
		LoadColorCurve(profile.fog(), result.fog);
		LoadColorCurve(profile.sun_scatter(), result.sunScatter);

		// Unset optional fields return their proto defaults, which equal the engine defaults.
		result.atmosphere.SetDensity(profile.fog_density());
		result.atmosphere.SetHeightFalloff(profile.fog_height_falloff());
		result.atmosphere.SetBaseHeight(profile.fog_base_height());
		result.atmosphere.SetAnisotropy(profile.fog_anisotropy());
		result.atmosphere.SetShaftStrength(profile.shaft_strength());

		result.exposure = std::clamp(profile.exposure(), 0.1f, 8.0f);
		result.bloomIntensity = std::clamp(profile.bloom_intensity(), 0.0f, 1.0f);
		result.bloomThreshold = std::clamp(profile.bloom_threshold(), 0.0f, 16.0f);
		result.transitionSeconds = std::clamp(profile.transition_seconds(), 0.0f, 30.0f);

		result.windDirectionDegrees = WrapDegrees360(profile.wind_direction());
		result.windSpeed = std::clamp(profile.wind_speed(), 0.0f, 30.0f);
		result.windGustiness = std::clamp(profile.wind_gustiness(), 0.0f, 1.0f);
		result.fogNoiseAmount = std::clamp(profile.fog_noise_amount(), 0.0f, 1.0f);
		result.fogNoiseSize = std::clamp(profile.fog_noise_size(), 5.0f, 500.0f);
		result.lightScattering = std::clamp(profile.light_scattering(), 0.0f, 8.0f);

		result.colorLut = profile.color_lut();
		result.saturation = std::clamp(profile.saturation(), 0.0f, 2.0f);
		result.contrast = std::clamp(profile.contrast(), 0.0f, 2.0f);
		result.colorFilter = Vector3(
			std::clamp(profile.color_filter_r(), 0.0f, 2.0f),
			std::clamp(profile.color_filter_g(), 0.0f, 2.0f),
			std::clamp(profile.color_filter_b(), 0.0f, 2.0f));

		return result;
	}

	/// @brief Resolves which environment profile applies in a zone of a map.
	/// @tparam TZones A ZoneManager (proto or proto_client).
	/// @tparam TMaps A MapManager (proto or proto_client).
	/// @param zoneId The zone the viewer stands in. 0 or unknown ids skip straight to the map.
	/// @param mapId The current map.
	/// @return The zone's own profile, else the nearest parent's (depth cap 8, so parent cycles
	///         terminate), else the map's default, else 0 for the built-in Default.
	template <typename TZones, typename TMaps>
	[[nodiscard]] uint32 ResolveEnvironmentProfileId(const TZones& zones, const TMaps& maps, const uint32 zoneId, const uint32 mapId)
	{
		const auto* zone = zoneId != 0 ? zones.getById(zoneId) : nullptr;
		for (int depth = 0; zone && depth < 8; ++depth)
		{
			if (zone->environment_profile() != 0)
			{
				return zone->environment_profile();
			}

			if (zone->parentzone() == 0)
			{
				break;
			}

			zone = zones.getById(zone->parentzone());
		}

		if (const auto* map = maps.getById(mapId))
		{
			return map->environment_profile();
		}

		return 0;
	}

	/// @brief Converts authored profiles on first use and hands out shared runtime profiles.
	/// @tparam TProfiles An EnvironmentProfileManager (proto or proto_client).
	/// @remark Id 0 and ids missing from the table resolve to EnvironmentProfile::GetDefault().
	///         Main thread only.
	template <typename TProfiles>
	class EnvironmentProfileCache final
	{
	public:
		/// @brief Creates a cache over a profile table that must outlive the cache.
		explicit EnvironmentProfileCache(const TProfiles& profiles)
			: m_profiles(profiles)
		{
		}

		/// @brief Gets the runtime profile for an id, converting it on first use.
		[[nodiscard]] std::shared_ptr<const EnvironmentProfile> Get(const uint32 id)
		{
			if (id == 0)
			{
				return EnvironmentProfile::GetDefault();
			}

			if (const auto it = m_cache.find(id); it != m_cache.end())
			{
				return it->second;
			}

			const auto* record = m_profiles.getById(id);
			if (!record)
			{
				return EnvironmentProfile::GetDefault();
			}

			auto profile = std::make_shared<const EnvironmentProfile>(LoadEnvironmentProfile(*record));
			m_cache.emplace(id, profile);
			return profile;
		}

		/// @brief Drops every cached conversion, e.g. after the table was edited.
		void Clear()
		{
			m_cache.clear();
		}

	private:
		const TProfiles& m_profiles;
		std::map<uint32, std::shared_ptr<const EnvironmentProfile>> m_cache;
	};
}
