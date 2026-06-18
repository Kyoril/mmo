// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#pragma once

#include "bot_action.h"
#include "bot_context.h"
#include "bot_movement_controller.h"

#include <sstream>

namespace mmo
{
	/// Action that moves the bot to a target position using navmesh pathfinding and client-style movement updates.
	class MoveToPositionAction final : public BotAction
	{
	public:
		explicit MoveToPositionAction(Vector3 targetPosition, float acceptanceRadius = 1.0f, float moveSpeed = 7.0f)
			: m_targetPosition(targetPosition)
			, m_acceptanceRadius(acceptanceRadius)
		{
			BotMovementSettings settings;
			settings.acceptanceRadius = acceptanceRadius;
			settings.fallbackRunSpeed = moveSpeed;
			m_movement = BotMovementController(settings);
		}

		std::string GetDescription() const override
		{
			std::ostringstream oss;
			oss << "Move to position ("
				<< m_targetPosition.x << ", "
				<< m_targetPosition.y << ", "
				<< m_targetPosition.z << ")";
			return oss.str();
		}

		ActionResult Execute(BotContext& context) override
		{
			if (!m_started)
			{
				m_started = true;
				if (!m_movement.MoveTo(context, m_targetPosition, m_acceptanceRadius))
				{
					return ActionResult::Failed;
				}
			}

			const BotMovementStatus status = m_movement.Update(context);
			switch (status)
			{
			case BotMovementStatus::Reached:
				return ActionResult::Success;
			case BotMovementStatus::Unreachable:
			case BotMovementStatus::Stuck:
				return ActionResult::Failed;
			case BotMovementStatus::Idle:
			case BotMovementStatus::Moving:
			default:
				return ActionResult::InProgress;
			}
		}

		void OnAbort(BotContext& context) override
		{
			m_movement.Stop(context, "action_aborted");
			m_started = false;
		}

		bool CanExecute(const BotContext& context, std::string& outReason) const override
		{
			if (!context.IsWorldReady())
			{
				outReason = "Bot is not in the world yet";
				return false;
			}

			if (!context.GetNavService() || !context.GetNavService()->IsReady())
			{
				outReason = "Navigation service is not available";
				return false;
			}

			return true;
		}

		bool IsInterruptible() const override
		{
			return true;
		}

	private:
		Vector3 m_targetPosition;
		float m_acceptanceRadius { 1.0f };
		bool m_started { false };
		BotMovementController m_movement;
	};
}
