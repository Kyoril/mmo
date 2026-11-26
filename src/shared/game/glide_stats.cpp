// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "glide_stats.h"

#include "game/item.h"

#include <algorithm>
#include <iterator>

namespace mmo
{
	namespace
	{
		float Fraction(const int32 percent, const int32 cap)
		{
			return static_cast<float>(std::clamp(percent, 0, cap)) / 100.0f;
		}
	}

	void AddGlideStat(GlideModifiers& modifiers, const uint32 statType, const int32 value)
	{
		switch (statType)
		{
		case item_stat::GlideSpeed:
			modifiers.speedPercent += value;
			break;
		case item_stat::GlideAgility:
			modifiers.agilityPercent += value;
			break;
		case item_stat::GlideLift:
			modifiers.liftPercent += value;
			break;
		case item_stat::GlideEndurance:
			modifiers.endurancePercent += value;
			break;
		default:
			break;
		}
	}

	GlideModifiers GetGlideModifiers(const ItemInfo& item)
	{
		GlideModifiers modifiers;
		for (const ItemStats& stat : item.stats)
		{
			AddGlideStat(modifiers, stat.type, static_cast<int32>(stat.value));
		}

		return modifiers;
	}

	TraversalSettings ApplyGlideModifiers(const TraversalSettings& base, const GlideModifiers& modifiers)
	{
		TraversalSettings result = base;
		result.glideForwardSpeed *= 1.0f + Fraction(modifiers.speedPercent, glide_stat_caps::Speed);
		result.glideTurnRate *= 1.0f + Fraction(modifiers.agilityPercent, glide_stat_caps::Agility);
		result.glideSinkSpeed *= 1.0f - Fraction(modifiers.liftPercent, glide_stat_caps::Lift);

		const float drainScale = 1.0f - Fraction(modifiers.endurancePercent, glide_stat_caps::Endurance);
		result.glideDrainPerSecond *= drainScale;
		result.glideDrainInUpdraftPerSecond *= drainScale;
		return result;
	}
}
