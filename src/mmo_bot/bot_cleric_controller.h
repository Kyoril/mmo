// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#pragma once

#include "bot_cleric_capabilities.h"
#include "bot_companion_types.h"

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace mmo
{
	enum class BotClericDecisionType : uint8
	{
		Hold = 0,
		CastSpell,
		/// Move toward a unit until it is within range for a follow-up action (heal or spell).
		Approach,
		/// Engage a hostile unit in melee (move into melee range and auto-attack).
		MeleeAttack,
	};

	struct BotClericUnitSnapshot final
	{
		uint64 guid { 0 };
		BotCompanionRole role { BotCompanionRole::None };
		bool isAlive { true };
		bool isAware { true };
		bool isInCombat { false };
		float healthPercent { 1.0f };
		float distanceToSelf { 0.0f };
		uint64 targetGuid { 0 };
		std::unordered_set<uint32> activeAuraSpellIds;
	};

	struct BotClericDecisionInput final
	{
		const BotClericCapabilities* capabilities { nullptr };
		BotCompanionMode companionMode { BotCompanionMode::Hold };
		bool spellbookReady { false };
		bool cooldownsReady { false };
		bool powerReady { false };
		/// True when fighting inside a dungeon or raid instance. Drives a more conservative
		/// healing mana reserve and disables the melee damage fallback.
		bool inDungeon { false };
		bool hasSelf { false };
		BotClericUnitSnapshot self;
		std::vector<BotClericUnitSnapshot> partyMembers;
		uint32 mana { 0 };
		uint32 maxMana { 0 };
		bool hasHostileTarget { false };
		bool hostileTargetAlive { false };
		bool hostileTargetHostile { false };
		uint64 hostileTargetGuid { 0 };
		float hostileTargetDistance { 0.0f };
		std::unordered_set<uint32> cooldownBlockedSpellIds;
	};

	struct BotClericDecision final
	{
		BotClericDecisionType type { BotClericDecisionType::Hold };
		std::string reason;
		uint32 spellId { 0 };
		uint64 castTargetGuid { 0 };
		bool castOnSelf { false };
		/// Target to move toward for Approach / MeleeAttack decisions.
		uint64 moveTargetGuid { 0 };
		/// Distance at which the bot should stop approaching the move target (yards).
		float moveDesiredRange { 0.0f };
	};

	class BotClericController final
	{
	public:
		[[nodiscard]] BotClericDecision Evaluate(const BotClericDecisionInput& input) const;
	};

	[[nodiscard]] const char* ToString(BotClericDecisionType type) noexcept;
}
