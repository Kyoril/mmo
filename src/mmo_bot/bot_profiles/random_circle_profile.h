// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#pragma once

#include "bot_profile.h"
#include "bot_actions/move_to_position_action.h"
#include "bot_actions/wait_action.h"

#include "log/default_log_levels.h"
#include "math/math_utils.h"

#include <chrono>
#include <random>

using namespace std::chrono_literals;

namespace mmo
{
	/// A bot profile that keeps choosing random points on a loose circle around its activation position.
	class RandomCircleProfile final : public BotProfile
	{
	public:
		std::string GetName() const override
		{
			return "Random Circle";
		}

	protected:
		void OnActivateImpl(BotContext& context) override
		{
			m_center = context.GetPosition();
			m_angle = m_angleDistribution(m_random);
			ILOG("Random circle profile activated around position ("
				<< m_center.x << ", " << m_center.y << ", " << m_center.z << ")");
			QueueNextMove();
		}

		bool OnQueueEmpty(BotContext& context) override
		{
			QueueNextMove();
			return true;
		}

	private:
		void QueueNextMove()
		{
			m_angle += m_angleStepDistribution(m_random);
			const float radius = m_radiusDistribution(m_random);
			const Vector3 target(
				m_center.x + std::sin(m_angle) * radius,
				m_center.y,
				m_center.z + std::cos(m_angle) * radius);

			QueueAction(std::make_shared<MoveToPositionAction>(target, 1.0f));
			QueueAction(std::make_shared<WaitAction>(250ms));
		}

	private:
		Vector3 m_center { Vector3::Zero };
		float m_angle { 0.0f };
		std::mt19937 m_random { std::random_device {}() };
		std::uniform_real_distribution<float> m_angleDistribution { 0.0f, Pi * 2.0f };
		std::uniform_real_distribution<float> m_angleStepDistribution { 0.65f, 1.45f };
		std::uniform_real_distribution<float> m_radiusDistribution { 5.0f, 9.0f };
	};
}
