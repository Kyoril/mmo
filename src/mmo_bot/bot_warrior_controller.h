// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#pragma once

#include "bot_companion_types.h"
#include "bot_warrior_capabilities.h"

#include <cstdint>
#include <string>
#include <unordered_set>

namespace mmo
{
	enum class BotWarriorDecisionType : uint8
	{
		Hold = 0,
		EnsureAutoAttack,
		CastSpell,
	};

	struct BotWarriorDecisionInput final
	{
		const BotWarriorCapabilities* capabilities { nullptr };
		BotCompanionMode companionMode { BotCompanionMode::Hold };
		bool spellbookReady { false };
		bool cooldownsReady { false };
		bool powerReady { false };
		bool selfInCombat { false };
		bool hasValidTarget { false };
		bool targetAlive { false };
		bool targetHostile { false };
		bool targetTargetingSelf { false };
		uint64 targetGuid { 0 };
		float targetDistance { 0.0f };
		uint32 rage { 0 };
		float selfHealthPercent { 1.0f };
		bool autoAttackActive { false };
		uint64 autoAttackTargetGuid { 0 };
		std::unordered_set<uint32> activeAuraSpellIds;
		std::unordered_set<uint32> cooldownBlockedSpellIds;
	};

	struct BotWarriorDecision final
	{
		BotWarriorDecisionType type { BotWarriorDecisionType::Hold };
		std::string reason;
		uint32 spellId { 0 };
		uint64 castTargetGuid { 0 };
		bool castOnSelf { false };
		bool ensureAutoAttack { false };
		uint64 autoAttackTargetGuid { 0 };
	};

	class BotWarriorController final
	{
	public:
		[[nodiscard]] BotWarriorDecision Evaluate(const BotWarriorDecisionInput& input) const;
	};

	[[nodiscard]] const char* ToString(BotWarriorDecisionType type) noexcept;
}
