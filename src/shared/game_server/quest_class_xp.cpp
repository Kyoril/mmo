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

		// Under-leveled class: grant the same fraction of the class's own level bar that the full
		// reward represents at the quest's intended level - the XP a quest designed for the class
		// level would have granted.
		const int32 clampedClassLevel = std::clamp(static_cast<int32>(classLevel), 1, maxClassLevel);
		const int32 clampedQuestLevel = std::clamp(questLevel, 1, maxClassLevel);
		if (clampedClassLevel >= clampedQuestLevel)
		{
			return rewardClassXp;
		}

		const uint32 classLevelBar = classEntry.classlevels(clampedClassLevel - 1).xptonextlevel();
		const uint32 questLevelBar = classEntry.classlevels(clampedQuestLevel - 1).xptonextlevel();
		if (questLevelBar == 0)
		{
			return rewardClassXp;
		}

		const uint64 scaled = static_cast<uint64>(rewardClassXp) * classLevelBar / questLevelBar;
		return static_cast<uint32>(std::max<uint64>(1, scaled));
	}
}
