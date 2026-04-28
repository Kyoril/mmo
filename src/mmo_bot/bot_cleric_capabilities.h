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
	enum class BotClericCapabilityKind : uint8
	{
		None = 0,
		EmergencyHeal,
		EfficientHeal,
		SupportAura,
		SupportBuff,
		Defensive,
		DamageFiller,
	};

	struct BotClericResolvedSpell final
	{
		BotClericCapabilityKind kind { BotClericCapabilityKind::None };
		uint32 spellId { 0 };
		uint32 rootSpellId { 0 };
		uint32 rank { 0 };
		uint32 classLearnLevel { 0 };
		uint32 powerCost { 0 };
		GameTime cooldownMs { 0 };
		GameTime castTimeMs { 0 };
		float minRange { 0.0f };
		float maxRange { 0.0f };
		bool hasExplicitRange { false };
		bool targetMetadataPresent { false };
		bool positiveMetadataPresent { false };
		bool requiresTarget { false };
		bool positive { false };
		bool passive { false };
		bool hidden { false };
		uint32 targetMap { 0 };
		uint32 school { 0 };
		std::string name;
		std::string description;

		[[nodiscard]] bool IsInRange(float distance) const noexcept;
	};

	struct BotClericCapabilities final
	{
		bool resolved { false };
		uint32 classId { 0 };
		uint32 spellFamily { 0 };
		PowerType powerType { power_type::Invalid_ };
		std::vector<BotClericResolvedSpell> spells;
		std::vector<std::string> issues;
		std::optional<BotClericResolvedSpell> emergencyHeal;
		std::optional<BotClericResolvedSpell> efficientHeal;
		std::optional<BotClericResolvedSpell> supportAura;
		std::optional<BotClericResolvedSpell> supportBuff;
		std::optional<BotClericResolvedSpell> defensive;
		std::optional<BotClericResolvedSpell> damageFiller;

		[[nodiscard]] size_t GetResolvedCategoryCount() const noexcept;
		[[nodiscard]] const BotClericResolvedSpell* FindSpell(uint32 spellId) const noexcept;
	};

	[[nodiscard]] BotClericCapabilities ResolveClericCapabilities(const proto::Project& project);
	[[nodiscard]] const char* ToString(BotClericCapabilityKind kind) noexcept;
}
