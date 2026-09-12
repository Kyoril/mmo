// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "water_volume_system.h"

namespace mmo
{
	bool WaterVolumeSystem::ResolveSubmersion(const float x, const float y, const float z,
		float& outSurfaceHeight, uint32& outType) const
	{
		outSurfaceHeight = 0.0f;
		outType = 0;

		// Presence first, always. Asking for the height of water that is not there returns 0,
		// which would make every point below sea level look submerged.
		if (!m_query.HasWaterAt(x, z))
		{
			return false;
		}

		const float surfaceHeight = m_query.GetWaterHeightAt(x, z);
		outSurfaceHeight = surfaceHeight;
		outType = m_query.GetWaterTypeAt(x, z);

		return y < surfaceHeight;
	}

	void WaterVolumeSystem::Update(const float cameraX, const float cameraY, const float cameraZ,
		const float playerX, const float playerY, const float playerZ, const float deltaSeconds)
	{
		float cameraSurfaceHeight = 0.0f;
		uint32 cameraType = 0;
		const bool cameraSubmerged = ResolveSubmersion(cameraX, cameraY, cameraZ, cameraSurfaceHeight, cameraType);

		float playerSurfaceHeight = 0.0f;
		uint32 playerType = 0;
		m_playerSubmerged = ResolveSubmersion(playerX, playerY, playerZ, playerSurfaceHeight, playerType);

		m_cameraWaterType = cameraSubmerged ? cameraType : 0;

		m_state.active = cameraSubmerged;
		m_state.surfaceHeight = cameraSurfaceHeight;
		m_state.submersionDepth = cameraSubmerged ? (cameraSurfaceHeight - cameraY) : 0.0f;
		m_state.transitionPhase = m_settings.AdvanceTransition(m_state.transitionPhase, cameraSubmerged, deltaSeconds);
		m_state.godRaysEnabled = m_godRaysEnabled;

		// Audio and the screen effect ramp independently, because they follow different things:
		// the screen effect follows the camera, the muffling follows the character. A third-person
		// camera sitting above the surface while the character swims must still sound submerged.
		m_audioPhase = m_settings.AdvanceTransition(m_audioPhase, m_playerSubmerged, deltaSeconds);

		// Remember the liquid driving the screen effect. While surfacing the camera is already
		// dry but the crossing is still unwinding, so the previous liquid's parameters must stay
		// until the phase reaches zero - otherwise the fog colour flashes off a frame early.
		if (cameraSubmerged)
		{
			m_lastScreenWaterType = cameraType;
		}
		else if (m_state.transitionPhase <= 0.0f)
		{
			m_lastScreenWaterType = 0;
		}

		// The audio profile follows the player's liquid for the same reason, and is held across
		// the audio crossing rather than the screen one.
		if (m_playerSubmerged)
		{
			m_lastAudioWaterType = playerType;
		}
		else if (m_audioPhase <= 0.0f)
		{
			m_lastAudioWaterType = 0;
		}

		WaterProfileValues profile;
		if (m_lastScreenWaterType != 0 && m_profileResolver)
		{
			profile = m_profileResolver(m_lastScreenWaterType);
		}

		if (profile.valid)
		{
			m_state.fogDensity = profile.fogDensity;
			m_state.fogColor[0] = profile.fogColor[0];
			m_state.fogColor[1] = profile.fogColor[1];
			m_state.fogColor[2] = profile.fogColor[2];
			m_state.absorptionColor[0] = profile.absorptionColor[0];
			m_state.absorptionColor[1] = profile.absorptionColor[1];
			m_state.absorptionColor[2] = profile.absorptionColor[2];
			m_state.causticsStrength = profile.causticsStrength;
			m_state.distortionStrength = profile.distortionStrength;
		}
		else
		{
			// No profile authored for this liquid. Zeroing rather than keeping the previous
			// liquid's values means an unauthored liquid renders as plain clear water instead of
			// inheriting, say, lava's fog.
			m_state.fogDensity = 0.0f;
			m_state.fogColor[0] = m_state.fogColor[1] = m_state.fogColor[2] = 0.0f;
			m_state.absorptionColor[0] = m_state.absorptionColor[1] = m_state.absorptionColor[2] = 0.0f;
			m_state.causticsStrength = 0.0f;
			m_state.distortionStrength = 0.0f;
		}

		// Audio follows the player, so it resolves its own profile and rides its own phase.
		WaterProfileValues audioProfile;
		if (m_lastAudioWaterType != 0 && m_profileResolver)
		{
			audioProfile = m_profileResolver(m_lastAudioWaterType);
		}

		m_audioLowPassHz = audioProfile.valid ? (audioProfile.audioLowPassHz * m_audioPhase) : 0.0f;
	}
}
