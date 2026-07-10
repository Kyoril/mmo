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
	/// An under-leveled class (class level below the quest level) is rewarded as if the quest had
	/// been designed for its own level: it receives the same fraction of its current class level
	/// bar that the full reward represents at the quest's intended level. This keeps banked
	/// high-level quests from power-leveling a fresh class while still granting meaningful
	/// (never zero) class XP for quests in the character's level range.
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
