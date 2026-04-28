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
	enum class BotMageCapabilityKind : uint8
	{
		None = 0,
		PrimaryNuke,
		InstantFallback,
		Control,
		SelfBuffEscape,
		EmergencySpacing,
	};

	struct BotMageResolvedSpell final
	{
		BotMageCapabilityKind kind { BotMageCapabilityKind::None };
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

	struct BotMageCapabilities final
	{
		bool resolved { false };
		uint32 classId { 0 };
		uint32 spellFamily { 0 };
		PowerType powerType { power_type::Invalid_ };
		std::vector<BotMageResolvedSpell> spells;
		std::vector<std::string> issues;
		std::optional<BotMageResolvedSpell> primaryNuke;
		std::optional<BotMageResolvedSpell> instantFallback;
		std::optional<BotMageResolvedSpell> control;
		std::optional<BotMageResolvedSpell> selfBuffEscape;
		std::optional<BotMageResolvedSpell> emergencySpacing;

		[[nodiscard]] size_t GetResolvedCategoryCount() const noexcept;
		[[nodiscard]] const BotMageResolvedSpell* FindSpell(uint32 spellId) const noexcept;
	};

	[[nodiscard]] BotMageCapabilities ResolveMageCapabilities(const proto::Project& project);
	[[nodiscard]] const char* ToString(BotMageCapabilityKind kind) noexcept;
}
