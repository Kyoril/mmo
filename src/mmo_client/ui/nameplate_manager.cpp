// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "nameplate_manager.h"

#include "nameplate_frame.h"

#include "console/console_var.h"
#include "frame_ui/frame_mgr.h"
#include "game_client/game_player_c.h"
#include "game_client/game_unit_c.h"
#include "game_client/object_mgr.h"

namespace mmo
{
	namespace
	{
		/// Reads a boolean console variable, falling back to a default if it doesn't exist.
		bool GetBoolCVar(const char* name, const bool defaultValue)
		{
			const ConsoleVar* var = ConsoleVarMgr::FindConsoleVar(name);
			return var ? var->GetBoolValue() : defaultValue;
		}
	}

	void NameplateManager::Update(const float elapsed, Camera& camera)
	{
		const auto player = ObjectMgr::GetActivePlayer();
		if (!player)
		{
			if (!m_nameplates.empty())
			{
				m_nameplates.clear();
				if (m_layer)
				{
					m_layer->RemoveAllChildren();
				}
			}
			return;
		}

		const Filters filters = ReadFilters();

		bool changed = false;

		// Drop plates whose units vanished or no longer pass the filters.
		for (auto it = m_nameplates.begin(); it != m_nameplates.end();)
		{
			const auto unit = ObjectMgr::Get<GameUnitC>(it->first);
			if (!unit || !ShouldShowNameplate(*unit, *player, filters))
			{
				it = m_nameplates.erase(it);
				changed = true;
			}
			else
			{
				++it;
			}
		}

		// Create plates for units which just started passing the filters.
		ObjectMgr::ForEachUnit([this, &player, &filters, &camera, &changed](GameUnitC& unit)
		{
			const ObjectGuid guid = unit.GetGuid();
			if (m_nameplates.find(guid) != m_nameplates.end())
			{
				return;
			}

			if (!ShouldShowNameplate(unit, *player, filters))
			{
				return;
			}

			if (!EnsureLayer())
			{
				return;
			}

			const std::string plateName = "Nameplate_" + std::to_string(++m_nameplateCounter);
			m_nameplates[guid] = std::make_shared<NameplateFrame>(plateName, camera, guid);
			changed = true;
		});

		// Rebuild the layer's children from the plate list so removed plates are released
		// and new ones are attached (Frame has no single-child removal).
		if (changed && m_layer)
		{
			m_layer->RemoveAllChildren();
			for (const auto& [guid, plate] : m_nameplates)
			{
				m_layer->AddChild(plate);
			}
		}

		for (const auto& [guid, plate] : m_nameplates)
		{
			plate->Animate(elapsed);
		}
	}

	void NameplateManager::Clear()
	{
		m_nameplates.clear();
		m_layer.reset();
	}

	NameplateManager::Filters NameplateManager::ReadFilters()
	{
		Filters filters;
		filters.enemyNpcs = GetBoolCVar("NameplateShowEnemyNpcs", true);
		filters.enemyPlayers = GetBoolCVar("NameplateShowEnemyPlayers", true);
		filters.friendlyNpcs = GetBoolCVar("NameplateShowFriendlyNpcs", false);
		filters.friendlyPlayers = GetBoolCVar("NameplateShowFriendlyPlayers", false);
		filters.enemyPets = GetBoolCVar("NameplateShowEnemyPets", false);
		filters.friendlyPets = GetBoolCVar("NameplateShowFriendlyPets", false);

		if (const ConsoleVar* distanceVar = ConsoleVarMgr::FindConsoleVar("NameplateDistance"))
		{
			filters.maxDistance = distanceVar->GetFloatValue();
		}

		return filters;
	}

	bool NameplateManager::ShouldShowNameplate(GameUnitC& unit, const GamePlayerC& player, const Filters& filters)
	{
		// Never show a plate for the player himself or for dead units.
		if (unit.GetGuid() == player.GetGuid())
		{
			return false;
		}

		if (!unit.IsAlive())
		{
			return false;
		}

		if (filters.maxDistance > 0.0f && !player.IsWithinRange(unit, filters.maxDistance))
		{
			return false;
		}

		const bool friendly = player.IsFriendlyTo(unit);

		if (unit.IsPlayer())
		{
			return friendly ? filters.friendlyPlayers : filters.enemyPlayers;
		}

		// Units owned by another unit count as pets (there is no pet system yet, but the
		// filter is future-proof for when summoned units arrive).
		if (unit.Get<uint64>(object_fields::Owner) != 0)
		{
			return friendly ? filters.friendlyPets : filters.enemyPets;
		}

		// Neutral NPCs (neither friendly nor hostile) share the enemy NPC filter and are
		// distinguished by their yellow bar color instead.
		return friendly ? filters.friendlyNpcs : filters.enemyNpcs;
	}

	bool NameplateManager::EnsureLayer()
	{
		if (m_layer)
		{
			return true;
		}

		const FramePtr worldFrame = FrameManager::Get().Find("WorldFrame");
		if (!worldFrame)
		{
			return false;
		}

		// Created directly (not registered with the FrameManager) so it is owned by the
		// WorldFrame and torn down together with the world UI on every world reload.
		m_layer = std::make_shared<Frame>("Frame", "NameplateLayer");
		m_layer->SetClickable(false);

		// Keep the layer above the chat bubble layer (frame level 0) regardless of which
		// of the two is created first: hit-testing only descends into the topmost
		// full-screen sibling, and nameplates must receive clicks while bubbles don't.
		m_layer->SetFrameLevel(1);

		worldFrame->AddChild(m_layer);

		// Fill the world frame so plate screen positions map 1:1 into this container.
		m_layer->SetAnchor(anchor_point::Left, anchor_point::Left, nullptr);
		m_layer->SetAnchor(anchor_point::Top, anchor_point::Top, nullptr);
		m_layer->SetAnchor(anchor_point::Right, anchor_point::Right, nullptr);
		m_layer->SetAnchor(anchor_point::Bottom, anchor_point::Bottom, nullptr);

		return true;
	}
}
