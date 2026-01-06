// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_profile.h"
#include "bot_context.h"
#include "bot_unit.h"
#include "bot_actions/wait_action.h"

#include "log/default_log_levels.h"

#include <chrono>

using namespace std::chrono_literals;

namespace mmo
{
	/// @brief A demonstration profile that logs information about nearby units.
	/// 
	/// This profile periodically scans for nearby units and logs their information,
	/// demonstrating the unit awareness system capabilities.
	class UnitAwarenessProfile final : public BotProfile
	{
	public:
		/// @brief Constructs the profile with a scan interval.
		/// @param scanIntervalMs How often to scan for nearby units (default: 5 seconds).
		explicit UnitAwarenessProfile(std::chrono::milliseconds scanIntervalMs = 5000ms)
			: m_scanInterval(scanIntervalMs)
		{
		}

		std::string GetName() const override
		{
			return "UnitAwareness";
		}

	protected:
		void OnActivateImpl(BotContext& context) override
		{
			ILOG("UnitAwareness profile activated - scanning for nearby units every " 
				<< m_scanInterval.count() << "ms");

			// Perform initial scan after a short delay (to allow packets to arrive)
			QueueAction(std::make_shared<WaitAction>(1000ms));
		}

		bool OnQueueEmpty(BotContext& context) override
		{
			// Perform a scan
			PerformUnitScan(context);

			// Queue next scan
			QueueAction(std::make_shared<WaitAction>(m_scanInterval));
			return true;
		}

	private:
		void PerformUnitScan(BotContext& context)
		{
			// Get the bot's own unit
			const BotUnit* self = context.GetSelf();
			if (!self)
			{
				WLOG("UnitAwareness: Bot not yet spawned, skipping scan");
				return;
			}

			const Vector3& myPos = self->GetPosition();
			const size_t totalUnits = context.GetUnitCount();

			ILOG("=== Unit Awareness Scan ===");
			ILOG("Bot position: (" << myPos.x << ", " << myPos.y << ", " << myPos.z << ")");
			ILOG("Total known units: " << totalUnits);

			// Get nearby units (40 yard radius)
			constexpr float scanRadius = 40.0f;
			auto nearbyUnits = context.GetNearbyUnits(scanRadius);
			ILOG("Units within " << scanRadius << " yards: " << nearbyUnits.size());

			// Categorize units
			size_t playerCount = 0;
			size_t creatureCount = 0;
			size_t hostileCount = 0;
			size_t friendlyCount = 0;

			for (const BotUnit* unit : nearbyUnits)
			{
				if (unit->GetGuid() == self->GetGuid())
				{
					continue;  // Skip self
				}

				if (unit->IsPlayer())
				{
					++playerCount;
				}
				else
				{
					++creatureCount;
				}

				if (unit->IsHostileTo(*self))
				{
					++hostileCount;
				}
				else if (unit->IsFriendlyTo(*self))
				{
					++friendlyCount;
				}

				// Log unit details
				const float distance = unit->GetDistanceTo(myPos);
				DLOG("  [" << (unit->IsPlayer() ? "Player" : "Creature") 
					<< "] GUID: " << std::hex << unit->GetGuid() << std::dec
					<< " | Entry: " << unit->GetEntry()
					<< " | Level: " << unit->GetLevel()
					<< " | HP: " << unit->GetHealth() << "/" << unit->GetMaxHealth()
					<< " (" << static_cast<int>(unit->GetHealthPercent() * 100) << "%)"
					<< " | Distance: " << distance << "y"
					<< " | " << (unit->IsHostileTo(*self) ? "HOSTILE" : "FRIENDLY"));
			}

			ILOG("Summary - Players: " << playerCount 
				<< " | Creatures: " << creatureCount
				<< " | Hostile: " << hostileCount 
				<< " | Friendly: " << friendlyCount);

			// Report specific queries
			if (const BotUnit* nearestHostile = context.GetNearestHostile())
			{
				ILOG("Nearest hostile: Entry " << nearestHostile->GetEntry()
					<< " at " << nearestHostile->GetDistanceTo(myPos) << " yards"
					<< " (HP: " << nearestHostile->GetHealthPercent() * 100 << "%)");
			}

			if (const BotUnit* nearestFriendlyPlayer = context.GetNearestFriendlyPlayer())
			{
				ILOG("Nearest friendly player: GUID " << std::hex << nearestFriendlyPlayer->GetGuid() << std::dec
					<< " at " << nearestFriendlyPlayer->GetDistanceTo(myPos) << " yards");
			}

			// Check if anything is targeting us
			auto targeting = context.GetUnitsTargetingSelf();
			if (!targeting.empty())
			{
				WLOG("WARNING: " << targeting.size() << " unit(s) targeting us!");
				for (const BotUnit* unit : targeting)
				{
					WLOG("  - " << (unit->IsPlayer() ? "Player" : "Creature") 
						<< " Entry: " << unit->GetEntry()
						<< " at " << unit->GetDistanceTo(myPos) << " yards");
				}
			}

			ILOG("=========================");
		}

	private:
		std::chrono::milliseconds m_scanInterval;
	};

}
