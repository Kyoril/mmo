// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "quest_class_xp.h"

#include "shared/proto_data/classes.pb.h"

#include <algorithm>

namespace mmo
{
	uint32 ScaleQuestClassXp(const uint32 rewardClassXp, const uint32 classLevel, const int32 questLevel, const proto::ClassEntry& classEntry)
	{
		if (rewardClassXp == 0)
		{
			return 0;
		}

		// A quest without an intended level provides no scaling baseline.
		if (questLevel <= 0)
		{
			return rewardClassXp;
		}

		// Without a class-level curve there is nothing to scale against (RewardClassExperience
		// drops the XP for such classes anyway).
		const int32 maxClassLevel = classEntry.classlevels_size();
		if (maxClassLevel == 0)
		{
			return rewardClassXp;
		}

		const int32 levelDiff = static_cast<int32>(classLevel) - questLevel;
		if (levelDiff >= 0)
		{
			// Over-leveled class: same step-down table as character quest XP.
			float xpFactor;
			if (levelDiff <= 5)
			{
				xpFactor = 1.0f;
			}
			else if (levelDiff == 6)
			{
				xpFactor = 0.8f;
			}
			else if (levelDiff == 7)
			{
				xpFactor = 0.6f;
			}
			else if (levelDiff == 8)
			{
				xpFactor = 0.4f;
			}
			else if (levelDiff == 9)
			{
				xpFactor = 0.2f;
			}
			else
			{
				xpFactor = 0.1f;
			}

			return std::max<uint32>(1, static_cast<uint32>(static_cast<float>(rewardClassXp) * xpFactor));
		}

		// Under-leveled class: a quest above the class level stays worth turning in. The reward only
		// decays once the class is more than five levels below the quest - the same grace window the
		// over-leveled side uses - and then by a thirtieth per level, down to a third of the reward at
		// twenty-five levels below. That keeps the protection for the case it was meant for (banking
		// very high level quests to power-level a fresh class once high levels exist) while a merely
		// higher level quest is no longer scaled below what a single creature kill grants, which is not
		// scaled down for a low class level at all.
		constexpr int32 graceLevels = 5;
		constexpr int32 scaleSteps = 30;
		constexpr int32 minSteps = 10;

		const int32 clampedClassLevel = std::clamp(static_cast<int32>(classLevel), 1, maxClassLevel);
		const int32 clampedQuestLevel = std::clamp(questLevel, 1, maxClassLevel);
		if (clampedClassLevel >= clampedQuestLevel)
		{
			return rewardClassXp;
		}

		const int32 levelsBelow = clampedQuestLevel - clampedClassLevel;
		const int32 steps = std::max(scaleSteps - std::max(levelsBelow - graceLevels, 0), minSteps);
		const uint64 granted = static_cast<uint64>(rewardClassXp) * static_cast<uint64>(steps) / static_cast<uint64>(scaleSteps);
		return static_cast<uint32>(std::max<uint64>(1, granted));
	}
}
