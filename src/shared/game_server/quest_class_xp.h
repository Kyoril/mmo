// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	namespace proto
	{
		class ClassEntry;
	}

	/// Scales a quest's designer-set class XP reward by the turn-in class level relative to the
	/// quest's intended level, using the same level-range language as mob and character quest XP.
	///
	/// An under-leveled class (class level below the quest level) keeps the full reward while it is
	/// within five levels of the quest level, and loses a thirtieth of the reward per level below
	/// that, down to a third of the reward at twenty-five levels below. The protection therefore only
	/// bites where it was meant to - banking very high level quests to power-level a fresh class -
	/// and a moderately higher level quest still grants far more than a single creature kill, whose
	/// class XP is not scaled down for a low class level at all. The quest level is clamped to the
	/// class max level, so a quest above everything the class can reach counts as a class-cap quest.
	///
	/// An over-leveled class steps down through the same factor table as character quest XP
	/// (full XP up to quest level + 5, then 80/60/40/20/10 percent).
	///
	/// @param rewardClassXp The designer-set class XP reward of the quest.
	/// @param classLevel The active class level of the player at turn-in.
	/// @param questLevel The quest's intended level; a value <= 0 disables scaling.
	/// @param classEntry The active class whose class-level curve provides the scaling baseline.
	/// @return The class XP to grant; at least 1 if rewardClassXp was non-zero.
	uint32 ScaleQuestClassXp(uint32 rewardClassXp, uint32 classLevel, int32 questLevel, const proto::ClassEntry& classEntry);
}
