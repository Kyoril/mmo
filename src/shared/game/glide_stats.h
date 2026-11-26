// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game/traversal.h"

namespace mmo
{
	struct ItemInfo;

	/// Glide stats of the equipped glider, in percent (item_stat::GlideSpeed .. GlideEndurance).
	struct GlideModifiers
	{
		/// Increase of TraversalSettings::glideForwardSpeed.
		int32 speedPercent = 0;

		/// Increase of TraversalSettings::glideTurnRate.
		int32 agilityPercent = 0;

		/// Reduction of TraversalSettings::glideSinkSpeed.
		int32 liftPercent = 0;

		/// Reduction of the glide Vigor drain (in and out of updrafts).
		int32 endurancePercent = 0;
	};

	/// Caps of the glide stats in percent. They keep the sink speed above zero (no climbing without
	/// an updraft) and the forward speed within what the server's glide validator allows.
	namespace glide_stat_caps
	{
		constexpr int32 Speed = 50;
		constexpr int32 Agility = 100;
		constexpr int32 Lift = 60;
		constexpr int32 Endurance = 75;
	}

	/// @brief Adds an item stat to the glide modifiers. Stat types other than the glide stats are ignored.
	/// @param modifiers The modifiers to add to.
	/// @param statType An item_stat type.
	/// @param value The stat value in percent.
	void AddGlideStat(GlideModifiers& modifiers, uint32 statType, int32 value);

	/// @brief Gets the glide modifiers of a glider's item data (the sum of its glide stats).
	GlideModifiers GetGlideModifiers(const ItemInfo& item);

	/// @brief Builds the effective glide settings from base settings and glide modifiers. Each
	///	modifier is clamped to 0 and its cap first; every other setting is copied unchanged.
	TraversalSettings ApplyGlideModifiers(const TraversalSettings& base, const GlideModifiers& modifiers);
}
