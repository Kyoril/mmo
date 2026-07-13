// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/localization.h"

#include <utility>
#include <vector>

namespace mmo
{
	namespace proto
	{
		class EmoteEntry;
	}

	/// Composes the localized chat line of an animated emote. Selects the best matching
	/// template (self / target / no-target, falling back to less specific ones), localizes
	/// it for the given locale and substitutes %s (source name) and %t (target name).
	/// @param emote The emote entry that was performed.
	/// @param locale The recipient locale used to resolve the template text.
	/// @param sourceName Name of the performing unit.
	/// @param targetName Name of the emote target (may be empty).
	/// @param hasTarget True when the emote had a target other than the performer.
	/// @param isSelf True when the emote targeted the performer itself.
	/// @returns The composed chat line, or an empty string for animation-only emotes.
	String ComposeEmoteText(const proto::EmoteEntry& emote, LocaleIndex locale, const String& sourceName, const String& targetName, bool hasTarget, bool isSelf);

	/// Advances a pose-variant selection to the next entry: default pose (0) -> first
	/// variant -> ... -> last variant -> default pose. A current selection that is not in
	/// the list (stale data) resets to the default pose.
	/// @param sortedVariants Available variants as (sort key, emote id) pairs, ascending.
	/// @param current The currently selected variant emote id (0 = default pose).
	/// @returns The next variant emote id (0 = default pose).
	uint32 SelectNextPoseVariant(const std::vector<std::pair<uint32, uint32>>& sortedVariants, uint32 current);
}
