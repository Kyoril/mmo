// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

namespace mmo
{
	/// Full-screen red edge vignette that intensifies as the local player's health drops,
	/// giving a strong "in danger" read during combat. Implemented as a screen layer, mirroring
	/// the LoadingScreen pattern. The vignette is invisible above a health threshold and pulses
	/// when the player is critically low.
	class CombatVignette final
	{
	public:
		/// Registers the vignette screen layer and loads its resources. Call once the graphics
		/// device and texture manager are available (e.g. on entering the world).
		static void Init();

		/// Removes the screen layer and releases resources.
		static void Destroy();

		/// Updates the danger factor that drives the vignette intensity.
		/// @param danger 0 = no vignette (healthy), 1 = maximum intensity (near death).
		static void SetDangerFactor(float danger);

	private:
		/// Screen layer paint callback; rebuilds and draws the vignette geometry.
		static void Paint();
	};
}
