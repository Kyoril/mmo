// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "ocean_pass.h"

#include "math/vector3.h"

#include <chrono>
#include <cmath>
#include <vector>

namespace mmo
{
	/// @brief Feeds the FFT ocean from a terrain once per frame: sea level, the camera-centred ocean
	///        mask and the ocean settings. Shared by the client and the world editor.
	///
	/// @tparam TTerrain terrain::Terrain. A template only so deferred_shading need not link the
	///         terrain library; it uses GetSeaLevel, GetOceanMaskRevision, BuildOceanMask and
	///         SetOceanRenderedExternally. The mask texel size (terrain::ocean_mask::TexelSize) is
	///         passed to the constructor for the same reason.
	template <typename TTerrain>
	class OceanFeed final
	{
	public:
		/// @brief Mask texels per side. One texel per water quad (~4.17 m): 768 cover 3.2 km.
		static constexpr uint32 MaskTexelsPerSide = 768;

		/// @brief Fraction of the window the camera may move before the window is re-centred.
		static constexpr float RecentreFraction = 0.125f;

		/// @brief Minimum time between two mask rebuilds caused by terrain changes (streaming).
		static constexpr std::chrono::milliseconds MinRebuildInterval { 250 };

	public:
		/// @brief Creates the feed.
		/// @param maskTexelSize World size of one ocean mask texel (terrain::ocean_mask::TexelSize).
		explicit OceanFeed(const float maskTexelSize)
			: m_maskTexelSize(maskTexelSize)
		{
		}

		/// @brief Updates the ocean for this frame. Hands ocean water to the pass (terrain stops
		///        meshing it) only while the pass is supported; otherwise the terrain keeps drawing it.
		/// @param ocean The renderer's ocean pass.
		/// @param terrain The terrain, or nullptr when no world is loaded (the ocean is switched off).
		/// @param cameraPosition The camera's world position.
		/// @param settings Ocean settings for this frame (from the Ocean water profile).
		void Update(OceanPass& ocean, TTerrain* terrain, const Vector3& cameraPosition, const OceanSettings& settings)
		{
			OceanSurfaceState state = ocean.GetSurfaceState();
			if (!terrain || !ocean.IsSupported())
			{
				if (terrain)
				{
					terrain->SetOceanRenderedExternally(false);
				}

				state.enabled = false;
				ocean.SetSurfaceState(state);
				m_terrain = nullptr;
				return;
			}

			if (m_terrain != terrain)
			{
				// A different world: forget the old mask entirely.
				m_terrain = terrain;
				m_revision = ~0ull;
				m_hasWindow = false;
			}

			terrain->SetOceanRenderedExternally(true);
			ocean.SetSettings(settings);

			const float texelSize = m_maskTexelSize;
			const float windowSize = texelSize * static_cast<float>(MaskTexelsPerSide);
			const bool moved = !m_hasWindow
				|| std::abs(cameraPosition.x - m_windowCentreX) > windowSize * RecentreFraction
				|| std::abs(cameraPosition.z - m_windowCentreZ) > windowSize * RecentreFraction;

			const uint64 revision = terrain->GetOceanMaskRevision();
			const auto now = std::chrono::steady_clock::now();
			const bool terrainChanged = revision != m_revision && now - m_lastRebuild >= MinRebuildInterval;

			if (moved || terrainChanged)
			{
				float originX = 0.0f;
				float originZ = 0.0f;
				terrain->BuildOceanMask(cameraPosition.x, cameraPosition.z, MaskTexelsPerSide, m_texels, originX, originZ);
				ocean.UploadMask(m_texels, MaskTexelsPerSide);

				m_windowCentreX = cameraPosition.x;
				m_windowCentreZ = cameraPosition.z;
				m_hasWindow = true;
				m_revision = revision;
				m_lastRebuild = now;

				state.maskOriginX = originX;
				state.maskOriginZ = originZ;
				state.maskTexelSize = texelSize;
			}

			state.enabled = true;
			state.seaLevel = terrain->GetSeaLevel();
			ocean.SetSurfaceState(state);
		}

	private:
		float m_maskTexelSize;
		TTerrain* m_terrain { nullptr };
		std::vector<uint8> m_texels;
		float m_windowCentreX { 0.0f };
		float m_windowCentreZ { 0.0f };
		bool m_hasWindow { false };
		uint64 m_revision { ~0ull };
		std::chrono::steady_clock::time_point m_lastRebuild {};
	};
}
