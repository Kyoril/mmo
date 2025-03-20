#pragma once

#include "base/non_copyable.h"
#include "proto_data/project.h"

namespace mmo
{
	class GamePlayerS;

	class ConditionMgr : public NonCopyable
	{
	public:
		explicit ConditionMgr(const proto::ConditionManager& data);

	public:
		bool PlayerMeetsCondition(GamePlayerS& player, uint32 conditionId);

	private:
		const proto::ConditionManager& m_data;
	};
}
