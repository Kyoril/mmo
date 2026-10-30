// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "combat_log_client.h"

#include "player_controller.h"
#include "frame_ui/frame_mgr.h"
#include "frame_ui/localizer.h"
#include "game/aura.h"
#include "game/damage_school.h"
#include "game/spell.h"
#include "game_client/combat_sound_player.h"
#include "game_client/game_player_c.h"
#include "game_client/game_unit_c.h"
#include "game_client/object_mgr.h"
#include "log/default_log_levels.h"
#include "shared/game_client/spell_visualization_service.h"

#include <string>
#include <vector>

namespace mmo
{
	namespace
	{
		/// Builds the attacker side of the combat sound context from a live unit.
		CombatSoundAttacker makeCombatAttacker(const GameUnitC& unit, const bool offhand)
		{
			CombatSoundAttacker attacker;
			attacker.weaponSubclass = unit.GetWeaponItemSubclass(offhand);
			attacker.displayId = unit.Get<uint32>(object_fields::DisplayId);
			return attacker;
		}

		/// Builds the victim side of the combat sound context from a live unit.
		CombatSoundVictim makeCombatVictim(const GameUnitC& unit)
		{
			CombatSoundVictim victim;
			victim.mainHandSubclass = unit.GetWeaponItemSubclass(false);
			victim.offHandSubclass = unit.GetWeaponItemSubclass(true);
			victim.chestSubclass = unit.GetChestItemSubclass();
			victim.displayId = unit.Get<uint32>(object_fields::DisplayId);
			return victim;
		}
	}

	CombatLogClient::CombatLogClient(RealmConnector &connector, const proto_client::Project &project, CombatSoundPlayer &combatSoundPlayer)
		: m_connector(connector)
		, m_project(project)
		, m_combatSoundPlayer(combatSoundPlayer)
	{
	}

	void CombatLogClient::Initialize(PlayerController &playerController, ICombatLogListener &listener)
	{
		m_playerController = &playerController;
		m_listener = &listener;

		m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::XpLog, *this, &CombatLogClient::OnXpLog);
		m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::SpellDamageLog, *this, &CombatLogClient::OnSpellDamageLog);
		m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::NonSpellDamageLog, *this, &CombatLogClient::OnNonSpellDamageLog);
		m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::EnvironmentalDamageLog, *this, &CombatLogClient::OnLogEnvironmentalDamage);
		m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::PeriodicAuraLog, *this, &CombatLogClient::OnPeriodicAuraLog);
		m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::SpellEnergizeLog, *this, &CombatLogClient::OnSpellEnergizeLog);
		m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::AttackerStateUpdate, *this, &CombatLogClient::OnAttackerStateUpdate);
		m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::SpellHealLog, *this, &CombatLogClient::OnSpellHealLog);
	}

	void CombatLogClient::Shutdown()
	{
		m_handlers.Clear();

		// m_listener stays set: swing-hit callbacks queued on units capture this client and may
		// still fire, and the listener (the world state) outlives this client anyway.
		m_playerController = nullptr;
	}

	PacketParseResult CombatLogClient::OnXpLog(game::IncomingPacket &packet)
	{
		uint32 xp;
		if (!(packet >> io::read<uint32>(xp)))
		{
			return PacketParseResult::Disconnect;
		}

		// Find unit
		std::shared_ptr<GamePlayerC> target = ObjectMgr::GetActivePlayer();
		if (target)
		{
			m_listener->AddWorldTextFrame(target->GetPosition(), std::to_string(xp) + " XP", Color(1.0f, 0.0f, 0.85f, 1.0f), 2.0f);
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult CombatLogClient::OnSpellDamageLog(game::IncomingPacket &packet)
	{
		uint64 targetGuid;
		uint32 amount;
		SpellSchool school;
		uint8 flags;
		uint32 spellId;
		uint32 blocked;

		if (!(packet >> io::read_packed_guid(targetGuid) >> io::read<uint32>(spellId) >> io::read<uint32>(amount) >> io::read<uint8>(school) >> io::read<uint8>(flags) >> io::read<uint32>(blocked)))
		{
			return PacketParseResult::Disconnect;
		}

		// Find unit
		std::shared_ptr<GameObjectC> target = ObjectMgr::Get<GameObjectC>(targetGuid);
		if (target)
		{
			// If the target was immune to this spell's damage school, show a localized "Immune"
			// text instead of a damage number.
			if (flags & damage_flags::Immune)
			{
				m_listener->AddWorldTextFrame(target->GetPosition(), Localize(FrameManager::Get().GetLocalization(), "COMBAT_IMMUNE"), Color(1.0f, 1.0f, 0.0f, 1.0f), 2.0f);
			}
			else if (blocked > 0 && amount == 0)
			{
				// The block fully absorbed the ability damage.
				m_listener->AddWorldTextFrame(target->GetPosition(), Localize(FrameManager::Get().GetLocalization(), "COMBAT_BLOCKED"), Color(1.0f, 1.0f, 0.0f, 1.0f), 2.0f);
			}
			else if (blocked > 0)
			{
				// Partial block: show remaining damage and how much was blocked, e.g. "14 (5 blocked)".
				const String blockedText = std::to_string(amount) + " (" + std::to_string(blocked) + " " +
					Localize(FrameManager::Get().GetLocalization(), "COMBAT_BLOCKED_SUFFIX") + ")";
				const bool isCritical = (flags & damage_flags::Crit) != 0;
				m_listener->AddWorldTextFrame(target->GetPosition(), blockedText, Color(1.0f, 1.0f, 0.0f, 1.0f), isCritical ? 4.0f : 2.0f,
					isCritical ? WorldTextAnimation::Critical : WorldTextAnimation::Normal);
			}
			else
			{
				const bool isCritical = (flags & damage_flags::Crit) != 0;
				m_listener->AddWorldTextFrame(target->GetPosition(), std::to_string(amount), Color(1.0f, 1.0f, 0.0f, 1.0f), isCritical ? 4.0f : 2.0f,
					isCritical ? WorldTextAnimation::Critical : WorldTextAnimation::Normal);
			}
		}

		if (amount > 0)
		{
			if (const auto player = ObjectMgr::GetActivePlayer())
			{
				FrameManager::Get().TriggerLuaEvent("DAMAGE_DONE",
					ObjectMgr::GetActivePlayerGuid(),
					player->GetName(),
					targetGuid,
					target ? target->GetName() : String(),
					amount,
					"SPELL");
			}
		}

		m_listener->TriggerCombatCameraShake(targetGuid, ObjectMgr::GetActivePlayerGuid(), amount, (flags & damage_flags::Crit) != 0);

		return PacketParseResult::Pass;
	}

	PacketParseResult CombatLogClient::OnNonSpellDamageLog(game::IncomingPacket &packet)
	{
		uint64 targetGuid;
		uint32 amount;
		DamageFlags flags;

		if (!(packet >> io::read_packed_guid(targetGuid) >> io::read<uint32>(amount) >> io::read<uint8>(flags)))
		{
			return PacketParseResult::Disconnect;
		}

		std::shared_ptr<GameObjectC> target = ObjectMgr::Get<GameObjectC>(targetGuid);
		if (target)
		{
			const bool isCritical = (flags & damage_flags::Crit) != 0;
			m_listener->AddWorldTextFrame(target->GetPosition(), std::to_string(amount), Color::White, isCritical ? 4.0f : 2.0f,
				isCritical ? WorldTextAnimation::Critical : WorldTextAnimation::Normal);
		}

		if (amount > 0)
		{
			if (const auto player = ObjectMgr::GetActivePlayer())
			{
				FrameManager::Get().TriggerLuaEvent("DAMAGE_DONE",
					ObjectMgr::GetActivePlayerGuid(),
					player->GetName(),
					targetGuid,
					target ? target->GetName() : String(),
					amount,
					"NON_SPELL");
			}
		}

		// TODO: Separate packet for this!
		if (m_playerController && m_playerController->GetControlledUnit())
		{
			m_playerController->GetControlledUnit()->NotifyAttackSwingEvent();
		}

		m_listener->TriggerCombatCameraShake(targetGuid, ObjectMgr::GetActivePlayerGuid(), amount, (flags & damage_flags::Crit) != 0);

		return PacketParseResult::Pass;
	}

	PacketParseResult CombatLogClient::OnAttackerStateUpdate(game::IncomingPacket &packet)
	{
		uint64 attackerGuid, attackedGuid;
		uint32 totalDamage, school, absorbedDamage, resistedDamage, blockedDamage;
		VictimState victimState;
		HitInfo hitInfo;
		if (!(packet >> io::read_packed_guid(attackerGuid) >> io::read_packed_guid(attackedGuid) >> io::read<uint32>(hitInfo) >> io::read<uint32>(victimState) >> io::read<uint32>(totalDamage) >> io::read<uint32>(school) >> io::read<uint32>(absorbedDamage) >> io::read<uint32>(resistedDamage) >> io::read<uint32>(blockedDamage)))
		{
			return PacketParseResult::Disconnect;
		}

		std::shared_ptr<GameUnitC> attacker = ObjectMgr::Get<GameUnitC>(attackerGuid);
		const bool offhandSwing = (hitInfo & hit_info::LeftSwing) != 0;
		const bool isCriticalSwing = (hitInfo & hit_info::CriticalHit) != 0;
		bool attackAnimationStarted = false;
		CombatSoundAttacker attackerContext;
		if (attacker)
		{
			// LeftSwing marks an off-hand (dual wield) swing so the dedicated off-hand attack
			// animation is played instead of the main-hand one.
			attackAnimationStarted = attacker->NotifyAttackSwingEvent(offhandSwing);

			attackerContext = makeCombatAttacker(*attacker, offhandSwing);

			// The swing itself is audible right away; everything that depends on the weapon
			// connecting is queued on the SwingHit notify further down.
			m_combatSoundPlayer.PlaySwing(attackerContext, attackerGuid, attacker->GetPosition(), isCriticalSwing);
		}

		std::shared_ptr<GameUnitC> attacked = ObjectMgr::Get<GameUnitC>(attackedGuid);
		if (attacked)
		{
			attacked->NotifyHitEvent();
		}

		// Combat audio runs for every swing in range, not only the local player's fights, so
		// it deliberately sits outside the isActivePlayerInvolved gate below. Out-of-range
		// sounds are culled by each SoundEntry's max distance.
		if (attacker && attacked)
		{
			CombatSwingOutcome outcome;
			outcome.crit = isCriticalSwing;

			// victimState is checked before the generic Miss flag: the server sets
			// hit_info::Miss for dodges and parries too, as a "no damage landed" marker.
			if ((hitInfo & hit_info::Immune) == 0 && victimState != victim_state::IsImmune)
			{
				if (victimState == victim_state::Dodge)
				{
					outcome.miss = true;
				}
				else if (victimState == victim_state::Parry)
				{
					outcome.parry = true;
				}
				else if (victimState == victim_state::Blocks)
				{
					outcome.block = true;
					outcome.fullBlock = totalDamage == 0 && (hitInfo & hit_info::Absorb) == 0;
					outcome.landed = !outcome.fullBlock;
				}
				else if (hitInfo & hit_info::Miss)
				{
					outcome.miss = true;
				}
				else
				{
					// A fully absorbed hit deals no damage but still connects.
					outcome.landed = true;
				}
			}

			auto soundCallback = [this,
				attackerContext,
				victimContext = makeCombatVictim(*attacked),
				outcome, attackedGuid, victimPos = attacked->GetPosition()]()
			{
				m_combatSoundPlayer.PlayOutcome(attackerContext, victimContext, outcome, attackedGuid, victimPos);
			};

			if (attackAnimationStarted)
			{
				attacker->QueueSwingHitCallback(std::move(soundCallback));
			}
			else
			{
				soundCallback();
			}
		}

		// Build the damage display callback. The floating text and Lua events are deferred to the
		// attacker's SwingHit animation notify so numbers appear when the weapon visually connects.
		// If the attacker has no current attack animation (or no SwingHit notify), the callback
		// fires immediately as a fallback.
		const bool isActivePlayerInvolved = ObjectMgr::GetActivePlayerGuid() == attackerGuid
			|| ObjectMgr::GetActivePlayerGuid() == attackedGuid;

		if (attacked && isActivePlayerInvolved)
		{
			// Determine display text now (all captured by value into the lambda).
			String damageText;
			// Check the specific victim states (dodge/parry/block) before the generic Miss flag:
			// the server sets hit_info::Miss for dodges and parries too (as a "no damage landed"
			// marker), so checking Miss first would mask them as "MISSED".
			if ((hitInfo & hit_info::Immune) || victimState == victim_state::IsImmune)
			{
				damageText = Localize(FrameManager::Get().GetLocalization(), "COMBAT_IMMUNE");
			}
			else if (victimState == victim_state::Dodge)
			{
				damageText = "DODGED";
			}
			else if (victimState == victim_state::Parry)
			{
				damageText = "PARRIED";
			}
			else if (victimState == victim_state::Blocks)
			{
				if (totalDamage == 0)
				{
					damageText = Localize(FrameManager::Get().GetLocalization(), "COMBAT_BLOCKED");
				}
				else
				{
					damageText = std::to_string(totalDamage) + " (" + std::to_string(blockedDamage) + " " +
						Localize(FrameManager::Get().GetLocalization(), "COMBAT_BLOCKED_SUFFIX") + ")";
				}
			}
			else if (hitInfo & hit_info::Miss)
			{
				damageText = "MISSED"; // Localize
			}
			else
			{
				damageText = std::to_string(totalDamage);
			}

			const bool isCritical = (hitInfo & hit_info::CriticalHit) != 0;
			const bool attackerIsPlayer = ObjectMgr::GetActivePlayerGuid() == attackerGuid;
			const Vector3 targetPos = attacked->GetPosition();
			const Color textColor = attackerIsPlayer ? Color::White : Color(1.0f, 0.0f, 0.0f);

			auto displayCallback = [this, targetPos, damageText, isCritical, textColor,
				attackerGuid, attackedGuid, totalDamage,
				attackerName = attacker ? attacker->GetName() : String(),
				attackedName = attacked->GetName(),
				attackerIsPlayer]()
			{
				m_listener->AddWorldTextFrame(targetPos, damageText, textColor,
					isCritical ? 4.0f : 2.0f, isCritical ? WorldTextAnimation::Critical : WorldTextAnimation::Normal);

				// Sync the camera shake with the moment the swing visually connects.
				m_listener->TriggerCombatCameraShake(attackedGuid, attackerGuid, totalDamage, isCritical);

				if (totalDamage > 0 && attackerIsPlayer)
				{
					FrameManager::Get().TriggerLuaEvent("DAMAGE_DONE",
						attackerGuid,
						attackerName,
						attackedGuid,
						attackedName,
						totalDamage,
						"MELEE");
				}
			};

			// Only defer to SwingHit notify when a new attack animation was actually started.
			// If the animation was suppressed (e.g. off-hand while main-hand is fresh) there is
			// nothing visual to sync to, so fire immediately.
			if (attacker && attackAnimationStarted)
			{
				attacker->QueueSwingHitCallback(std::move(displayCallback));
			}
			else
			{
				displayCallback();
			}
		}
		else if (totalDamage > 0 && ObjectMgr::GetActivePlayerGuid() == attackerGuid && attacker)
		{
			// Attacker is the local player but the attacked unit isn't in the client's range —
			// still fire the Lua event (no world text since there's no position to attach to).
			FrameManager::Get().TriggerLuaEvent("DAMAGE_DONE",
				attackerGuid,
				attacker->GetName(),
				attackedGuid,
				attacked ? attacked->GetName() : String(),
				totalDamage,
				"MELEE");
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult CombatLogClient::OnLogEnvironmentalDamage(game::IncomingPacket &packet)
	{
		uint64 targetGuid;
		uint32 amount;
		uint8 damageType;

		if (!(packet >> io::read_packed_guid(targetGuid) >> io::read<uint32>(amount) >> io::read<uint8>(damageType)))
		{
			return PacketParseResult::Disconnect;
		}

		// Find the target unit to display floating combat text
		std::shared_ptr<GameObjectC> target = ObjectMgr::Get<GameObjectC>(targetGuid);
		if (target)
		{
			// Environmental damage is displayed in red-orange to distinguish from regular damage
			m_listener->AddWorldTextFrame(target->GetPosition(), std::to_string(amount), Color(1.0f, 0.5f, 0.0f, 1.0f), 2.0f);
		}

		// Environmental damage has no attacker; only the victim (the local player) shakes.
		m_listener->TriggerCombatCameraShake(targetGuid, 0, amount, false);

		return PacketParseResult::Pass;
	}

	PacketParseResult CombatLogClient::OnPeriodicAuraLog(game::IncomingPacket &packet)
	{
		uint64 targetGuid;
		uint64 casterGuid;
		uint32 spellId;
		uint32 auraType;

		if (!(packet >> io::read_packed_guid(targetGuid) >> io::read_packed_guid(casterGuid) >> io::read<uint32>(spellId) >> io::read<uint32>(auraType)))
		{
			ELOG("Failed to read PeriodicAuraLog packet!");
			return PacketParseResult::Disconnect;
		}

		// Trigger AuraTick visualization event
		if (const auto* spell = m_project.spells.getById(spellId))
		{
			GameUnitC* casterUnit = nullptr;
			if (const auto caster = ObjectMgr::Get<GameUnitC>(casterGuid))
			{
				casterUnit = caster.get();
			}

			if (std::shared_ptr<GameUnitC> targetUnit = ObjectMgr::Get<GameUnitC>(targetGuid))
			{
				std::vector<GameUnitC*> targets { targetUnit.get() };
				SpellVisualizationService::Get().Apply(SpellVisualizationService::Event::AuraTick, *spell, casterUnit, targets);
			}
		}

		if (auraType == aura_type::PeriodicDamage ||
			auraType == aura_type::PeriodicHeal)
		{
			Color color;
			if (auraType == aura_type::PeriodicHeal)
			{
				color = Color(0.0f, 1.0f, 0.0f, 1.0f);
			}
			else
			{
				color = Color(1.0f, 1.0f, 0.0f, 1.0f);
			}

			uint32 amount;
			if (!(packet >> io::read<uint32>(amount)))
			{
				ELOG("Failed to read amount from PeriodicAuraLog packet!");
				return PacketParseResult::Disconnect;
			}

			if (casterGuid == ObjectMgr::GetActivePlayerGuid() || targetGuid == ObjectMgr::GetActivePlayerGuid())
			{
				if (const std::shared_ptr<GameObjectC> target = ObjectMgr::Get<GameObjectC>(targetGuid))
				{
					m_listener->AddWorldTextFrame(target->GetPosition(), std::to_string(amount), color, 2.0f);
				}
			}

			if (auraType == aura_type::PeriodicDamage && amount > 0 && casterGuid == ObjectMgr::GetActivePlayerGuid())
			{
				const std::shared_ptr<GameUnitC> caster = ObjectMgr::Get<GameUnitC>(casterGuid);
				const std::shared_ptr<GameObjectC> target = ObjectMgr::Get<GameObjectC>(targetGuid);

				FrameManager::Get().TriggerLuaEvent("DAMAGE_DONE",
					casterGuid,
					caster ? caster->GetName() : String(),
					targetGuid,
					target ? target->GetName() : String(),
					amount,
					"PERIODIC");
			}
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult CombatLogClient::OnSpellEnergizeLog(game::IncomingPacket &packet)
	{
		uint64 targetGuid, casterGuid;
		uint32 spellId, powerType, power;
		if (!(packet >> io::read_packed_guid(targetGuid) >> io::read_packed_guid(casterGuid) >> io::read<uint32>(spellId) >> io::read<uint32>(powerType) >> io::read<uint32>(power)))
		{
			ELOG("Failed to read SpellEnergizeLog packet!");
			return PacketParseResult::Disconnect;
		}

		// TODO: Combat log event

		return PacketParseResult::Pass;
	}

	PacketParseResult CombatLogClient::OnSpellHealLog(game::IncomingPacket &packet)
	{
		uint64 targetGuid, casterGuid;
		uint32 amount;
		bool crit;
		uint32 spellId;

		if (!(packet >> io::read_packed_guid(targetGuid) >> io::read_packed_guid(casterGuid) >> io::read<uint32>(spellId) >> io::read<uint32>(amount) >> io::read<uint8>(crit)))
		{
			return PacketParseResult::Disconnect;
		}

		// If the target or the caster is our player, we want to display the heal amount
		if (casterGuid == ObjectMgr::GetActivePlayerGuid() || targetGuid == ObjectMgr::GetActivePlayerGuid())
		{
			// Find unit
			std::shared_ptr<GameObjectC> target = ObjectMgr::Get<GameObjectC>(targetGuid);
			if (target)
			{
				m_listener->AddWorldTextFrame(target->GetPosition(), std::to_string(amount), Color(0.0f, 1.0f, 0.0f, 1.0f), crit ? 4.0f : 2.0f,
					crit ? WorldTextAnimation::Critical : WorldTextAnimation::Normal);
			}
		}

		return PacketParseResult::Pass;
	}
}
