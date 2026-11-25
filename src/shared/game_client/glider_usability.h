// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

namespace mmo
{
	/// @brief Whether the own client may open the glider, and if not, whether to tell the player why.
	namespace glider_usability
	{
		enum Type
		{
			/// The glider can be opened.
			Usable,
			/// The glider spell is known, but no glider is equipped: the player is told so.
			NoGliderEquipped,
			/// Not possible right now (no spell, no Vigor, dead, rooted or moved by the server);
			///	nothing is shown.
			Unavailable
		};
	}

	/// @brief Input of EvaluateGliderUsability, read from the own player each time.
	struct GliderUsabilityInput
	{
		bool knowsGlider{false};
		bool hasGliderEquipped{false};
		float predictedVigor{0.0f};
		bool alive{false};
		bool rooted{false};
		bool serverControlled{false};
	};

	/// @brief Decides whether the own client may open the glider. Mirrors the server's glide start
	///	check (GamePlayerS::CanUseTraversal plus the Vigor and state checks).
	[[nodiscard]] glider_usability::Type EvaluateGliderUsability(const GliderUsabilityInput& input);
}
