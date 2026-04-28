// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#pragma once

#include "game/spell.h"
#include "proto_data/project.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mmo
{
	enum class BotWarriorCapabilityKind : uint8
	{
		None = 0,
		GapCloser,
		ThreatBuilder,
		ArmorDebuff,
		RageBuilder,
		PartyBuff,
		Mitigation,
		RageSpender,
		Taunt,
	};

	struct BotWarriorResolvedSpell final
	{
		BotWarriorCapabilityKind kind { BotWarriorCapabilityKind::None };
		uint32 spellId { 0 };
		uint32 rootSpellId { 0 };
		uint32 rank { 0 };
		uint32 classLearnLevel { 0 };
		uint32 powerCost { 0 };
		GameTime cooldownMs { 0 };
		float minRange { 0.0f };
		float maxRange { 0.0f };
		bool hasExplicitRange { false };
		bool requiresTarget { false };
		bool positive { false };
		bool passive { false };
		bool hidden { false };
		uint32 targetMap { 0 };
		std::string name;
		std::string description;

		[[nodiscard]] bool IsInRange(const float distance) const noexcept;
	};

	struct BotWarriorCapabilities final
	{
		bool resolved { false };
		uint32 classId { 0 };
		uint32 spellFamily { 0 };
		PowerType powerType { power_type::Invalid_ };
		uint32 mainhandAutoAttackSpellId { 0 };
		std::vector<BotWarriorResolvedSpell> spells;
		std::vector<std::string> issues;
		std::optional<BotWarriorResolvedSpell> gapCloser;
		std::optional<BotWarriorResolvedSpell> threatBuilder;
		std::optional<BotWarriorResolvedSpell> armorDebuff;
		std::optional<BotWarriorResolvedSpell> rageBuilder;
		std::optional<BotWarriorResolvedSpell> partyBuff;
		std::optional<BotWarriorResolvedSpell> mitigation;
		std::optional<BotWarriorResolvedSpell> rageSpender;
		std::optional<BotWarriorResolvedSpell> taunt;

		[[nodiscard]] size_t GetResolvedCategoryCount() const noexcept;
		[[nodiscard]] const BotWarriorResolvedSpell* FindSpell(const uint32 spellId) const noexcept;
	};

	[[nodiscard]] BotWarriorCapabilities ResolveWarriorCapabilities(const proto::Project& project);
	[[nodiscard]] const char* ToString(BotWarriorCapabilityKind kind) noexcept;
}
