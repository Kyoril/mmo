// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/condition_mgr.h"
#include "game_server/objects/game_player_s.h"
#include "log/default_log_levels.h"

namespace mmo
{
	ConditionMgr::ConditionMgr(const proto::ConditionManager& data)
		: m_data(data)
	{
		DLOG("Condition Manager set up - loaded " << m_data.count() << " conditions!");
	}

	bool ConditionMgr::PlayerMeetsCondition(GamePlayerS& player, uint32 conditionId)
	{
        auto* cond = m_data.getById(conditionId);
        if (!cond)
        {
			ELOG("Tried to validate non existent condition " << conditionId);
            return false;
        }

        // Evaluate sub-conditions first, if any
        bool subResult = true; // or false if logicOperator == OR
        if (!cond->subconditionids().empty())
        {
            switch (cond->logicoperator())
            {
            case proto::Condition_LogicOperator_AND:
            {
                for (const auto subId : cond->subconditionids())
                {
                    if (!PlayerMeetsCondition(player, subId))
                    {
                        subResult = false;
                        break;
                    }
                }
                break;
            }
            case proto::Condition_LogicOperator_OR:
            {
                subResult = false;
                for (const auto subId : cond->subconditionids())
                {
                    if (PlayerMeetsCondition(player, subId))
                    {
                        subResult = true;
                        break;
                    }
                }
                break;
            }
            default:
                // If logicOperator=NONE but we have subConditionIds,
                // decide how you want to handle that. 
                break;
            }
        }

        // If it's a group condition, subResult is your final result
        // *unless* you also want to combine it with the main type check:
        if (cond->conditiontype() == proto::Condition_ConditionType_NONE_TYPE)
        {
            // It's just a container for subconditions
            return subResult;
        }

        // Evaluate the "main" condition
        bool selfResult = false;
        switch (cond->conditiontype())
        {
        case proto::Condition_ConditionType_CLASS_CHECK:
            // e.g. param1 = required class ID
            ASSERT(player.GetClassEntry());
            selfResult = (player.GetClassEntry()->id() == cond->param1());
            break;

        case proto::Condition_ConditionType_LEVEL_CHECK:
            // e.g. param1 = minLevel, param2 = maxLevel
        {
            const uint32 lvl = player.GetLevel();
            const uint32 minLvl = cond->param1();
            uint32 maxLvl = cond->param2();
            if (maxLvl == 0)
            {
                maxLvl = 255;
            }
            selfResult = (lvl >= minLvl && lvl <= maxLvl);
        }
        break;

		case proto::Condition_ConditionType_QUEST_CHECK:
			// e.g. param1 = required quest ID, param2 = QuestStatus
			selfResult = player.GetQuestStatus(cond->param1()) == cond->param2();
			break;

        default:
            // no known condition type
            break;
        }

        return subResult && selfResult;
	}
}
