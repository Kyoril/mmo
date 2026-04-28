// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#pragma once

#include "bot_companion_types.h"
#include "bot_mage_capabilities.h"

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace mmo
{
	enum class BotMageDecisionType : uint8
	{
		Hold = 0,
		CastSpell,
		EmergencySpacing,
	};

	struct BotMageSelfSnapshot final
	{
		uint64 guid { 0 };
		bool isAlive { true };
		float healthPercent { 1.0f };
		uint32 mana { 0 };
		uint32 maxMana { 0 };
		std::unordered_set<uint32> activeAuraSpellIds;
	};

	struct BotMageHostileSnapshot final
	{
		uint64 guid { 0 };
		bool isAlive { true };
		bool isAware { true };
		bool isHostile { true };
		bool targetingSelf { false };
		float distanceToSelf { 0.0f };
	};

	struct BotMageDecisionInput final
	{
		const BotMageCapabilities* capabilities { nullptr };
		BotCompanionMode companionMode { BotCompanionMode::Hold };
		bool spellbookReady { false };
		bool cooldownsReady { false };
		bool powerReady { false };
		bool hasSelf { false };
		BotMageSelfSnapshot self;
		bool hasPrimaryTarget { false };
		BotMageHostileSnapshot primaryTarget;
		std::vector<BotMageHostileSnapshot> nearbyHostiles;
		std::unordered_set<uint32> cooldownBlockedSpellIds;
	};

	struct BotMageDecision final
	{
		BotMageDecisionType type { BotMageDecisionType::Hold };
		std::string reason;
		uint32 spellId { 0 };
		uint64 castTargetGuid { 0 };
		bool castOnSelf { false };
	};

	class BotMageController final
	{
	public:
		[[nodiscard]] BotMageDecision Evaluate(const BotMageDecisionInput& input) const;
	};

	[[nodiscard]] const char* ToString(BotMageDecisionType type) noexcept;
}
