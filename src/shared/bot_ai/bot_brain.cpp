// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_brain.h"

#include "bot_ai_registry.h"
#include "base/clock.h"

#include "bot_core/bot_context.h"
#include "bot_core/bot_movement_controller.h"
#include "bot_core/bot_realm_connector.h"
#include "bot_core/bot_session.h"
#include "bot_core/bot_unit.h"
#include "game/item.h"

#include <utility>

namespace mmo
{
	BotBrain::BotBrain(BotSession& session, const BotAiRegistry& registry, const uint32 botIndex, BotBrainSettings settings)
		: m_context(&session, botIndex)
		, m_engines(registry)
		, m_settings(std::move(settings))
	{
		m_engines.GetEngine(BotAiState::NonCombat).AddStrategy(m_settings.nonCombatStrategy);
		m_engines.GetEngine(BotAiState::NonCombat).AddStrategy(m_settings.economyStrategy);
		m_engines.GetEngine(BotAiState::NonCombat).AddStrategy(m_settings.progressionStrategy);
		m_engines.GetEngine(BotAiState::NonCombat).AddStrategy(m_settings.supportNonCombatStrategy);
		m_engines.GetEngine(BotAiState::Combat).AddStrategy(m_settings.supportCombatStrategy);
		m_engines.GetEngine(BotAiState::Combat).AddStrategy(m_settings.combatStrategy);
		m_engines.GetEngine(BotAiState::Dead).AddStrategy(m_settings.deadStrategy);

		// A fixed offset per bot rather than a random one: the swarm stays spread out, and a bot
		// keeps the same phase across restarts, so a run can be compared with the one before it.
		if (m_settings.decisionJitterMs > 0)
		{
			m_decisionOffsetMs = botIndex % m_settings.decisionJitterMs;
		}

		for (const BotAiState state : { BotAiState::NonCombat, BotAiState::Combat, BotAiState::Dead })
		{
			m_engines.GetEngine(state).ActionExecuted.connect([this](const std::string& action, const float relevance)
				{
					ActionExecuted(action, relevance);
				});
		}

		m_engines.StateChanged.connect([this](const BotAiState from, const BotAiState to)
			{
				StateChanged(from, to);
			});

		// The server tells the bot exactly why a swing was refused. That is far better information
		// than the bot could work out for itself - it cannot see its own facing as the server
		// computes it, nor the reach the server uses - so the reaction is driven by the error
		// rather than by prediction.
		m_swingErrorConnection = session.GetRealm().AttackSwingError.connect(
			[this](const AttackSwingEvent event)
			{
				m_context.NoteSwingError(event, GetAsyncTimeMs());
			});

		// ...and just as precisely when the swing started landing again, so the bot stops reacting
		// to a problem it already walked out of.
		// The loot window opens a moment after the bot asked for it. Everything in it that the
		// bot may take is taken at once, as a player clicking "loot all" would, and the window
		// closed; the corpse is remembered so the bot does not walk back to it.
		m_lootOpenedConnection = session.GetRealm().LootOpened.connect(
			[this, &session](const BotLootWindow& window)
			{
				BotEconomyState& economy = m_context.GetEconomyState();
				BotRealmConnector& realm = session.GetRealm();

				if (window.money > 0)
				{
					realm.LootMoney();
				}

				for (const BotLootItem& item : window.items)
				{
					if (item.slotType == loot_slot_type::AllowLoot)
					{
						realm.AutoStoreLootItem(item.slot);
					}
				}

				realm.LootRelease(window.lootGuid);
				economy.lootedCorpses[window.lootGuid] = GetAsyncTimeMs();
				if (economy.pendingLootGuid == window.lootGuid)
				{
					economy.pendingLootGuid = 0;
				}
			});

		m_lootFailedConnection = session.GetRealm().LootFailed.connect(
			[this](const uint64 lootGuid, uint8)
			{
				BotEconomyState& economy = m_context.GetEconomyState();
				economy.lootedCorpses[lootGuid] = GetAsyncTimeMs();
				if (economy.pendingLootGuid == lootGuid)
				{
					economy.pendingLootGuid = 0;
				}
			});

		// An error right after an equip means the bot cannot use that item - wrong armor type,
		// a weapon its class does not train. It is not tried again.
		m_inventoryErrorConnection = session.GetRealm().InventoryErrorReceived.connect(
			[this](uint8)
			{
				BotEconomyState& economy = m_context.GetEconomyState();
				if (economy.lastEquipEntry != 0 && GetAsyncTimeMs() - economy.lastEquipMs < 3000)
				{
					economy.unusableItems.insert(economy.lastEquipEntry);
					economy.lastEquipEntry = 0;
				}
			});

		m_swingRecoveredConnection = session.GetRealm().AttackSwingRecovered.connect(
			[this]()
			{
				m_context.ClearSwingError();
			});
	}

	void BotBrain::SetGrindSpots(const GrindSpotIndex* spots)
	{
		m_context.SetGrindSpots(spots);
	}

	void BotBrain::SetRole(const std::string& role, const TalentBuildBook* builds)
	{
		m_context.SetRole(role);
		m_context.SetTalentBuilds(builds);
	}

	void BotBrain::SetGrindClaims(GrindSpotClaims* claims)
	{
		m_context.SetGrindClaims(claims);
	}

	void BotBrain::SetNpcIndex(const NpcIndex* npcs)
	{
		m_context.SetNpcIndex(npcs);
	}

	void BotBrain::SetFollowTarget(const uint64 guid)
	{
		BotFollowState& follow = m_context.GetFollowState();
		const bool wasFollowing = follow.IsFollowing();
		follow.leaderGuid = guid;

		if (wasFollowing == follow.IsFollowing())
		{
			return;
		}

		follow.pathMs = 0;

		BotEngine& nonCombat = m_engines.GetEngine(BotAiState::NonCombat);
		BotEngine& combat = m_engines.GetEngine(BotAiState::Combat);
		nonCombat.ClearStrategies();
		combat.ClearStrategies();
		combat.AddStrategy(m_settings.combatStrategy);

		if (follow.IsFollowing())
		{
			nonCombat.AddStrategy(m_settings.partyNonCombatStrategy);
			combat.AddStrategy(m_settings.partyCombatStrategy);
		}
		else
		{
			nonCombat.AddStrategy(m_settings.nonCombatStrategy);
		}
		nonCombat.AddStrategy(m_settings.economyStrategy);
		nonCombat.AddStrategy(m_settings.progressionStrategy);
		nonCombat.AddStrategy(m_settings.supportNonCombatStrategy);
		combat.AddStrategy(m_settings.supportCombatStrategy);

		// A grind spot picked before the party is not where the party is going, and one picked
		// while following is wherever the leader happened to stand.
		m_context.GetGrindState().ClearSpot();

		if (BotSession* session = m_context.GetSession())
		{
			BotMovementController& mover = session->GetMovementController();
			if (mover.IsActive())
			{
				mover.Stop(session->GetContext(), follow.IsFollowing() ? "joined_party" : "left_party");
			}
		}
	}

	void BotBrain::Reset()
	{
		m_engines.Reset();
		m_context.GetGrindState().ClearSpot();
		m_context.GetGrindState().blacklist.clear();
		m_lastLiveTargetGuid = 0;
		m_nextDecisionMs = 0;
	}

	void BotBrain::DetectKill()
	{
		const BotPerception& perception = m_context.GetPerception();

		BotSession* session = m_context.GetSession();
		if (!session)
		{
			return;
		}

		const BotContext& world = session->GetContext();

		if (m_lastLiveTargetGuid != 0)
		{
			// Ask about the unit itself rather than about the current target, so that switching
			// targets is not mistaken for a kill. Only a corpse counts. A unit that is simply gone from the object manager left our
			// sight or despawned - most often because the bot walked away from it - and counting
			// that inflates the one number the swarm report passes or fails the run on.
			const BotUnit* previous = world.GetUnit(m_lastLiveTargetGuid);
			if (!previous)
			{
				m_lastLiveTargetGuid = 0;
			}
			else if (!previous->IsAlive())
			{
				++m_context.GetGrindState().kills;
				TargetKilled(m_lastLiveTargetGuid);
				m_lastLiveTargetGuid = 0;
			}
		}

		if (perception.targetExists && perception.targetAlive && perception.IsInCombat())
		{
			m_lastLiveTargetGuid = perception.targetGuid;
		}
	}

	void BotBrain::Update(const GameTime nowMs)
	{
		if (m_nextDecisionMs == 0)
		{
			// The first decision is offset, and every later one is spaced from it, so the phase
			// this bot ends up on is the one it keeps.
			m_nextDecisionMs = nowMs + m_decisionOffsetMs;
		}

		if (nowMs < m_nextDecisionMs)
		{
			return;
		}

		m_nextDecisionMs = nowMs + m_settings.decisionIntervalMs;

		BotSession* session = m_context.GetSession();
		if (!session || !session->IsWorldReady())
		{
			return;
		}

		m_context.SetNow(nowMs);

		// One read of the world per decision, shared by every trigger and action that follows.
		m_context.RefreshPerception();
		m_context.RefreshRotation();

		DetectKill();

		static_cast<void>(m_engines.Update(m_context));
	}
}
