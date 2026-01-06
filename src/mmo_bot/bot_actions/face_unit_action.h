// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_action.h"
#include "bot_context.h"
#include "bot_unit.h"

#include "log/default_log_levels.h"

#include <sstream>

namespace mmo
{
	/// @brief Action that faces the bot towards a target unit.
	/// 
	/// This is a simple action that immediately faces the target and completes.
	class FaceUnitAction final : public BotAction
	{
	public:
		/// @brief Constructs a face-unit action.
		/// @param targetGuid The GUID of the unit to face.
		explicit FaceUnitAction(uint64 targetGuid)
			: m_targetGuid(targetGuid)
		{
		}

		std::string GetDescription() const override
		{
			std::ostringstream oss;
			oss << "Face unit 0x" << std::hex << m_targetGuid;
			return oss.str();
		}

		ActionResult Execute(BotContext& context) override
		{
			// Get the target unit
			const BotUnit* target = context.GetUnit(m_targetGuid);
			if (!target)
			{
				WLOG("[FACE] Target unit no longer exists");
				return ActionResult::Failed;
			}

			// Face the target
			context.FaceUnit(m_targetGuid);
			DLOG("[FACE] Turned to face target");

			// Complete immediately - the facing packet is sent
			return ActionResult::Success;
		}

		void OnAbort(BotContext& context) override
		{
			// Nothing to clean up
		}

		/// Face actions are interruptible.
		bool IsInterruptible() const override
		{
			return true;
		}

	private:
		uint64 m_targetGuid;
	};

}
