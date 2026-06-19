// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#pragma once

#include "../bot_action.h"
#include "../bot_cleric_controller.h"
#include "../bot_companion_state_machine.h"
#include "../bot_context.h"
#include "../bot_mage_controller.h"
#include "../bot_warrior_controller.h"
#include "follow_leader_action.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace mmo
{
	enum class CompanionFollowPreconditionFailure : uint8
	{
		None = 0,
		SelfUnavailable,
		InvalidSelfPosition,
		NavUnavailable,
		MapUnavailable,
	};

	struct CompanionFollowControllerInput final
	{
		BotCompanionSnapshot snapshot;
		BotFollowSample self;
		BotFollowPath path;
		BotFollowState state;
		GameTime now { 0 };
		CompanionFollowPreconditionFailure preconditionFailure { CompanionFollowPreconditionFailure::None };
	};

	struct CompanionFollowControllerOutput final
	{
		BotCompanionResult companion;
		BotFollowDecision followDecision;
		bool shouldRequestPath { false };
	};

	class CompanionFollowController final
	{
	public:
		explicit CompanionFollowController(BotFollowConfig config = {})
			: m_followController(std::move(config))
		{
		}

		[[nodiscard]] CompanionFollowControllerOutput Evaluate(const CompanionFollowControllerInput& input) const
		{
			CompanionFollowControllerOutput output;
			output.companion = m_stateMachine.Evaluate(input.snapshot);

			if (input.preconditionFailure != CompanionFollowPreconditionFailure::None)
			{
				output.followDecision.type =
					(input.preconditionFailure == CompanionFollowPreconditionFailure::NavUnavailable
						|| input.preconditionFailure == CompanionFollowPreconditionFailure::MapUnavailable)
					? BotFollowDecisionType::Abort
					: BotFollowDecisionType::Hold;
				output.followDecision.reason =
					input.preconditionFailure == CompanionFollowPreconditionFailure::SelfUnavailable ? "self_unavailable"
					: input.preconditionFailure == CompanionFollowPreconditionFailure::InvalidSelfPosition ? "self_anchor_invalid"
					: input.preconditionFailure == CompanionFollowPreconditionFailure::NavUnavailable ? "nav_unavailable"
					: input.preconditionFailure == CompanionFollowPreconditionFailure::MapUnavailable ? "map_unresolved"
					: "";
				output.followDecision.shouldStop = true;
				return output;
			}

			if (!output.companion.hasAnchor)
			{
				output.followDecision.type = BotFollowDecisionType::Hold;
				output.followDecision.reason = output.companion.modeReason;
				output.followDecision.shouldStop = true;
				return output;
			}

			if (!output.companion.anchor.hasPosition || !IsFiniteVector(output.companion.anchor.position))
			{
				output.followDecision.type = BotFollowDecisionType::Hold;
				output.followDecision.reason = "invalid_anchor_position";
				output.followDecision.shouldStop = true;
				return output;
			}

			FollowLeaderControllerInput followInput;
			followInput.anchor = output.companion.anchor;
			followInput.self = input.self;
			followInput.path = input.path;
			followInput.state = input.state;
			followInput.now = input.now;

			const FollowLeaderControllerOutput followOutput = m_followController.Evaluate(followInput);
			output.followDecision = followOutput.decision;
			output.shouldRequestPath = followOutput.shouldRequestPath;
			return output;
		}
		[[nodiscard]] const BotFollowConfig& GetConfig() const noexcept { return m_followController.GetConfig(); }

	private:
		BotCompanionStateMachine m_stateMachine;
		FollowLeaderController m_followController;
	};

	class CompanionFollowAction final : public BotAction
	{
	public:
		explicit CompanionFollowAction(BotFollowConfig config = {}, float moveSpeed = 7.0f);

		std::string GetDescription() const override;
		ActionResult Execute(BotContext& context) override;
		void OnAbort(BotContext& context) override;
		bool IsInterruptible() const override;

	private:
		[[nodiscard]] CompanionFollowControllerInput BuildControllerInput(BotContext& context, GameTime now) const;
		void ApplyDecision(BotContext& context, const CompanionFollowControllerInput& input, const CompanionFollowControllerOutput& output);
		void RequestPath(BotContext& context, const CompanionFollowControllerInput& input, const CompanionFollowControllerOutput& output);
		void AdvanceAlongPath(BotContext& context, const CompanionFollowControllerInput& input, const CompanionFollowControllerOutput& output);
		void StopMovement(BotContext& context);
		void PrepareUnitAction(BotContext& context, uint64 targetGuid);

		[[nodiscard]] std::string BuildDecisionDetails(BotContext& context, const CompanionFollowControllerInput& input, const CompanionFollowControllerOutput& output) const;
		void LogCompanionModeOnce(const CompanionFollowControllerOutput& output, const std::string& details);
		void LogAnchorDecisionOnce(const CompanionFollowControllerOutput& output, const std::string& details);
		void UpdatePreviousCompanionState(const CompanionFollowControllerOutput& output);

		void RefreshWarriorCapabilities(BotContext& context);
		void ExecuteWarriorRuntime(BotContext& context, const CompanionFollowControllerInput& input, const CompanionFollowControllerOutput& output);
		void ObserveWarriorCastFailure(BotContext& context, const CompanionFollowControllerOutput& output);
		void LogWarriorActionOnce(std::string_view action, std::string_view reason, const std::string& details);
		void LogWarriorFailureOnce(std::string_view reason, const std::string& details);

		void RefreshClericCapabilities(BotContext& context);
		void ExecuteClericRuntime(BotContext& context, const CompanionFollowControllerInput& input, const CompanionFollowControllerOutput& output);
		void ObserveClericCastFailure(BotContext& context, const CompanionFollowControllerOutput& output);
		void LogClericActionOnce(std::string_view action, std::string_view reason, const std::string& details);
		void LogClericFailureOnce(std::string_view reason, const std::string& details);

		void RefreshMageCapabilities(BotContext& context);
		void ExecuteMageRuntime(BotContext& context, const CompanionFollowControllerInput& input, const CompanionFollowControllerOutput& output);
		void ObserveMageCastFailure(BotContext& context, const CompanionFollowControllerOutput& output);
		void LogMageActionOnce(std::string_view action, std::string_view reason, const std::string& details);
		void LogMageFailureOnce(std::string_view reason, const std::string& details);

		void ExecuteAutoAttackRuntime(BotContext& context, const CompanionFollowControllerInput& input, const CompanionFollowControllerOutput& output);
		void LogAutoAttackOnce(std::string_view action, const std::string& details);

	private:
		static constexpr GameTime kHeartbeatIntervalMs { 500 };

		CompanionFollowController m_controller;
		BotCompanionState m_previousCompanionState;
		BotFollowState m_state;
		BotFollowPath m_path;
		GameTime m_lastHeartbeat { 0 };
		GameTime m_lastSimulationTime { 0 };
		float m_moveSpeed { 7.0f };
		std::string m_lastModeLogKey;
		std::string m_lastAnchorLogKey;
		std::string m_lastWarriorActionLogKey;
		std::string m_lastWarriorFailureLogKey;
		std::string m_lastClericActionLogKey;
		std::string m_lastClericFailureLogKey;
		std::string m_lastMageActionLogKey;
		std::string m_lastMageFailureLogKey;
		std::string m_lastAutoAttackLogKey;
		BotWarriorController m_warriorController;
		BotWarriorCapabilities m_warriorCapabilities;
		std::string m_warriorCapabilityIssue;
		bool m_hasWarriorCapabilities { false };
		BotClericController m_clericController;
		BotClericCapabilities m_clericCapabilities;
		std::string m_clericCapabilityIssue;
		bool m_hasClericCapabilities { false };
		BotMageController m_mageController;
		BotMageCapabilities m_mageCapabilities;
		std::string m_mageCapabilityIssue;
		bool m_hasMageCapabilities { false };
		std::unordered_map<uint64, float> m_lastObservedPartyHealthPercents;
		uint64 m_pendingAutoAttackTargetGuid { 0 };
		GameTime m_pendingAutoAttackRequestedAt { 0 };
		GameTime m_lastObservedWarriorCastFailureAt { 0 };
		uint32 m_lastObservedWarriorCastFailureSpellId { 0 };
		SpellCastResult m_lastObservedWarriorCastFailureReason { spell_cast_result::CastOkay };
		GameTime m_lastObservedClericCastFailureAt { 0 };
		uint32 m_lastObservedClericCastFailureSpellId { 0 };
		SpellCastResult m_lastObservedClericCastFailureReason { spell_cast_result::CastOkay };
		GameTime m_lastObservedMageCastFailureAt { 0 };
		uint32 m_lastObservedMageCastFailureSpellId { 0 };
		SpellCastResult m_lastObservedMageCastFailureReason { spell_cast_result::CastOkay };
	};
}
