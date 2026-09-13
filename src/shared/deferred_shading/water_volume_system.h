// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "deferred_shading/underwater_settings.h"

#include <functional>

namespace mmo
{
	/// @brief Read-only access to the water surface the camera and the player are moving through.
	///
	/// @remark WaterVolumeSystem takes this rather than a terrain::Terrain so it carries no
	///			dependency on the terrain library, which lets deferred_shading_tests compile it
	///			directly on the headless build and lets the tests describe a coastline in three lines.
	class IWaterQuery
	{
	public:
		virtual ~IWaterQuery() = default;

		/// @brief Whether any water surface exists at a world position.
		/// @remark Must be answered from water presence data, never from the surface height.
		///			Terrain water heights are zero-initialised, so a height of 0 means "no water"
		///			exactly as often as it means "surface at sea level".
		[[nodiscard]] virtual bool HasWaterAt(float x, float z) const = 0;

		/// @brief The world Y of the water surface at a position. Only meaningful where
		///			HasWaterAt is true.
		[[nodiscard]] virtual float GetWaterHeightAt(float x, float z) const = 0;

		/// @brief The liquid type at a position, as a terrain::WaterType value. 0 means none.
		[[nodiscard]] virtual uint32 GetWaterTypeAt(float x, float z) const = 0;
	};

	/// @brief The presentation settings of one liquid, resolved from the water profile table.
	struct WaterProfileValues
	{
		/// @brief False when no profile exists for the requested liquid type. Every other field
		///			is then left at its default rather than holding stale values.
		bool valid{ false };

		float fogDensity{ 0.0f };
		float fogColor[3]{ 0.0f, 0.0f, 0.0f };
		float absorptionColor[3]{ 0.0f, 0.0f, 0.0f };
		float causticsStrength{ 0.0f };
		float distortionStrength{ 0.0f };
		float audioLowPassHz{ 0.0f };
	};

	/// @brief Unpacks a 0xAARRGGBB colour, as water profiles store them, into float RGB in [0,1].
	/// @param packed The packed colour. Alpha is ignored.
	/// @param outRgb Receives red, green and blue.
	inline void UnpackWaterProfileColor(const uint32 packed, float outRgb[3])
	{
		outRgb[0] = static_cast<float>((packed >> 16) & 0xFFu) / 255.0f;
		outRgb[1] = static_cast<float>((packed >> 8) & 0xFFu) / 255.0f;
		outRgb[2] = static_cast<float>(packed & 0xFFu) / 255.0f;
	}

	/// @brief Converts an authored water profile into the values WaterVolumeSystem consumes.
	/// @tparam TProfile The editor's proto::WaterProfile or the client's proto_client::WaterProfile.
	///			The two generated classes mirror each other field for field, so one template serves
	///			the editor viewport and the client alike without this header depending on either
	///			protobuf library - and the two can never disagree about what a profile means.
	/// @param profile The authored profile.
	/// @return Valid values. Every field the profile leaves unset resolves to 0, disabling that effect.
	template <typename TProfile>
	[[nodiscard]] WaterProfileValues ToWaterProfileValues(const TProfile& profile)
	{
		WaterProfileValues values;
		values.valid = true;

		if (profile.has_fog_color())
		{
			UnpackWaterProfileColor(profile.fog_color(), values.fogColor);
		}

		if (profile.has_absorption_color())
		{
			UnpackWaterProfileColor(profile.absorption_color(), values.absorptionColor);
		}

		values.fogDensity = profile.has_fog_density() ? profile.fog_density() : 0.0f;
		values.causticsStrength = profile.has_caustics_strength() ? profile.caustics_strength() : 0.0f;
		values.distortionStrength = profile.has_distortion_strength() ? profile.distortion_strength() : 0.0f;
		values.audioLowPassHz = profile.has_audio_lowpass_hz() ? profile.audio_lowpass_hz() : 0.0f;

		return values;
	}

	/// @brief Tracks whether the camera and the player are submerged, and produces the state the
	///			underwater post-process pass and the audio low-pass consume.
	///
	/// @remark Pure logic: no graphics, no Lua, no signals, no resource managers. Update takes two
	///			positions and a frame time and returns state.
	///
	/// @remark The camera and the player are tracked separately on purpose. A third-person camera
	///			routinely dips below the surface while the character is still walking on dry sand,
	///			and the screen effect follows the camera while the audio and swim state follow the
	///			character. The world editor has no character and passes its camera for both.
	class WaterVolumeSystem final
	{
	public:
		/// @brief Creates the system.
		/// @param query Source of water surface information. Must outlive this object.
		explicit WaterVolumeSystem(const IWaterQuery& query)
			: m_query(query)
		{
		}

		/// @brief Sets the callback resolving a liquid type to its presentation settings.
		/// @param resolver The callback. When unset, every liquid resolves to an invalid profile
		///			and the underwater treatment runs with zeroed parameters rather than reading
		///			uninitialised values.
		void SetProfileResolver(std::function<WaterProfileValues(uint32)> resolver)
		{
			m_profileResolver = std::move(resolver);
		}

		/// @brief Overrides the crossing transition tunables.
		void SetSettings(const UnderwaterSettings& settings) { m_settings = settings; }

		/// @brief Enables or disables sun shafts, from the gxUnderwaterGodRays cvar.
		void SetGodRaysEnabled(const bool enabled) { m_godRaysEnabled = enabled; }

		/// @brief Recomputes submersion for one frame.
		/// @param cameraX World X of the camera.
		/// @param cameraY World Y of the camera.
		/// @param cameraZ World Z of the camera.
		/// @param playerX World X of the player.
		/// @param playerY World Y of the player (eye or head height, whatever the caller treats
		///			as the submersion reference).
		/// @param playerZ World Z of the player.
		/// @param deltaSeconds Frame time in seconds.
		void Update(float cameraX, float cameraY, float cameraZ,
			float playerX, float playerY, float playerZ, float deltaSeconds);

		/// @brief The state for the renderer this frame.
		[[nodiscard]] const UnderwaterState& GetState() const { return m_state; }

		/// @brief Whether the player character is submerged, independent of the camera.
		[[nodiscard]] bool IsPlayerSubmerged() const { return m_playerSubmerged; }

		/// @brief The audio low-pass cutoff in Hz for this frame, ramped over the crossing.
		///			0 means no filtering.
		[[nodiscard]] float GetAudioLowPassHz() const { return m_audioLowPassHz; }

		/// @brief The liquid type the camera is currently in, or 0 when dry.
		[[nodiscard]] uint32 GetCameraWaterType() const { return m_cameraWaterType; }

		/// @brief The liquid whose settings drive the screen effect, or 0 when there is none.
		/// @remark Unlike GetCameraWaterType this stays set while the surfacing crossing unwinds.
		///			Anything presented together with the screen effect - the caustics texture - has
		///			to follow this one, or it drops out while the fog is still fading.
		[[nodiscard]] uint32 GetScreenWaterType() const { return m_lastScreenWaterType; }

	private:
		/// @brief Returns true when the given point lies below a water surface, and writes that
		///			surface's height and liquid type out.
		bool ResolveSubmersion(float x, float y, float z, float& outSurfaceHeight, uint32& outType) const;

		const IWaterQuery& m_query;
		std::function<WaterProfileValues(uint32)> m_profileResolver;

		UnderwaterSettings m_settings;
		UnderwaterState m_state;

		bool m_playerSubmerged{ false };
		float m_audioLowPassHz{ 0.0f };
		uint32 m_cameraWaterType{ 0 };
		bool m_godRaysEnabled{ true };

		/// @brief Crossing progress for the audio filter. Separate from the screen crossing
		///			because audio follows the player and the screen effect follows the camera.
		float m_audioPhase{ 0.0f };

		/// @brief Liquid whose settings drive the screen effect, held across the crossing so the
		///			fog does not flash off while surfacing.
		uint32 m_lastScreenWaterType{ 0 };

		/// @brief Liquid whose settings drive the audio filter, held across the audio crossing.
		uint32 m_lastAudioWaterType{ 0 };
	};
}
