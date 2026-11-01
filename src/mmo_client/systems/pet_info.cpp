// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "pet_info.h"

#include "base/clock.h"
#include "frame_ui/frame_mgr.h"
#include "game/guid_utils.h"
#include "game/pet_defines.h"
#include "game/spell.h"
#include "game/spell_target_map.h"
#include "game_client/game_unit_c.h"
#include "game_client/object_mgr.h"
#include "log/default_log_levels.h"

#include "luabind_lambda.h"

#include <algorithm>

namespace mmo
{
	namespace
	{
		/// Localization key of a pet_result shown to the player, nullptr for Ok. The pet results that
		/// mirror a spell cast error reuse its text.
		const char* GetPetResultKey(const uint8 result)
		{
			switch (result)
			{
			case pet_result::Ok:
				return nullptr;
			case pet_result::TooManyPets:
				return "SPELL_CAST_FAILED_TOO_MANY_PETS";
			case pet_result::NoPet:
				return "SPELL_CAST_FAILED_NO_PET";
			case pet_result::PetDead:
				return "SPELL_CAST_FAILED_PET_DEAD";
			case pet_result::NotTamable:
				return "SPELL_CAST_FAILED_NOT_TAMABLE";
			case pet_result::TooHighLevel:
				return "SPELL_CAST_FAILED_TARGET_TOO_HIGH_LEVEL";
			case pet_result::NoFreeSlot:
				return "SPELL_CAST_FAILED_NO_FREE_PET_SLOT";
			case pet_result::AlreadyHavePet:
				return "SPELL_CAST_FAILED_ALREADY_HAVE_PET";
			case pet_result::WrongClass:
				return "SPELL_CAST_FAILED_PET_WRONG_CLASS";
			case pet_result::InvalidName:
				return "PET_RESULT_INVALID_NAME";
			case pet_result::NamingNotAllowed:
				return "PET_RESULT_NAMING_NOT_ALLOWED";
			case pet_result::Disabled:
				return "PET_RESULT_DISABLED";
			default:
				return "PET_RESULT_UNKNOWN";
			}
		}

		/// Reports a pet error to the UI.
		void FirePetResult(const char* key)
		{
			if (key != nullptr)
			{
				FrameManager::Get().TriggerLuaEvent("PET_RESULT", key);
			}
		}

		/// Fires a unit event for "pet". The target frame has its own observers (WorldState).
		void FirePetUnitEvent(const char* eventName)
		{
			FrameManager::Get().TriggerLuaEvent(eventName, "pet");
		}
	}

	PetInfo::PetInfo(RealmConnector& realmConnector, const proto_client::SpellManager& spells)
		: m_realmConnector(realmConnector)
		, m_spellManager(spells)
	{
	}

	PetInfo::~PetInfo() = default;

	void PetInfo::Initialize()
	{
		ASSERT(m_packetHandlerHandles.IsEmpty());

		m_packetHandlerHandles += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::PetSpells, *this, &PetInfo::OnPetSpells);
		m_packetHandlerHandles += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::PetLearnedSpell, *this, &PetInfo::OnPetLearnedSpell);
		m_packetHandlerHandles += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::PetXp, *this, &PetInfo::OnPetXp);
		m_packetHandlerHandles += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::PetNameQueryResult, *this, &PetInfo::OnPetNameQueryResult);
		m_packetHandlerHandles += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::PetResult, *this, &PetInfo::OnPetResult);
	}

	void PetInfo::Shutdown()
	{
		m_packetHandlerHandles.Clear();
		m_petObservers.disconnect();

		m_pet = game::PetSpellsData();
		m_spells.clear();
		m_xp = 0;
		m_nextLevelXp = 0;
		m_temporaryPets.clear();
		m_cooldowns.clear();
		m_pendingNameQueries.clear();
		m_lastFailureSpell = 0;
		m_lastFailureResult = 0;
		m_lastFailureTime = 0;
		ObjectMgr::SetActivePetGuid(0);

		// Pet names stay cached: a pet keeps its guid, and renames are broadcast to everyone in sight.
	}

	void PetInfo::RegisterScriptFunctions(lua_State* lua)
	{
#ifdef __INTELLISENSE__
#pragma warning(disable : 28)
#endif
		LUABIND_MODULE(lua,
			luabind::def_lambda("HasPetUI", [this]() { return HasPetUI(); }),
			luabind::def_lambda("HasPersistentPet", [this]() { return HasPersistentPet(); }),
			luabind::def_lambda("HasTemporaryPets", [this]() { return HasTemporaryPets(); }),
			luabind::def_lambda("GetNumPetSpells", [this]() { return GetNumSpells(); }),
			luabind::def_lambda("GetPetSpell", [this](const int32 index) { return GetSpell(index); }),
			luabind::def_lambda("PetCastSpell", [this](const int32 index) { CastSpell(index); }),
			luabind::def_lambda("GetPetSpellCooldown", [this](const int32 index) { return GetSpellCooldown(index); }),
			luabind::def_lambda("GetPetSpellCooldownDuration", [this](const int32 index) { return GetSpellCooldownDuration(index); }),
			luabind::def_lambda("PetAttack", [this]() { Attack(); }),
			luabind::def_lambda("PetFollow", [this]() { Follow(); }),
			luabind::def_lambda("PetStay", [this]() { Stay(); }),
			luabind::def_lambda("PetSetStrategy", [this](const int32 strategy) { SetStrategy(static_cast<uint32>(std::max(0, strategy))); }),
			luabind::def_lambda("GetPetStrategy", [this]() { return static_cast<int32>(GetStrategy()); }),
			luabind::def_lambda("GetPetLevelMode", [this]() { return static_cast<int32>(GetLevelMode()); }),
			luabind::def_lambda("PetDismiss", [this]() { Dismiss(); }),
			luabind::def_lambda("PetRename", [this](const String& name) { Rename(name); }),
			luabind::def_lambda("CanRenamePet", [this]() { return CanRename(); }),
			luabind::def<std::function<void(uint32&, uint32&)>>("GetPetXp", [this](uint32& out_xp, uint32& out_next)
				{
					out_xp = m_xp;
					out_next = m_nextLevelXp;
				}, luabind::meta::join<luabind::pure_out_value<1>, luabind::pure_out_value<2>>::type())
		);
#ifdef __INTELLISENSE__
#pragma warning(default : 28)
#endif
	}

	void PetInfo::OnUnitSpawned(GameUnitC& unit)
	{
		const uint64 guid = unit.GetGuid();

		if (IsPetGUID(guid))
		{
			if (const auto it = m_petNames.find(guid); it != m_petNames.end())
			{
				unit.SetPetName(it->second);
			}
			else
			{
				QueryPetName(guid);
			}
		}

		if (guid == m_pet.petGuid)
		{
			ObservePetUnit(unit);
			FrameManager::Get().TriggerLuaEvent("PET_CHANGED");
			return;
		}

		const uint64 playerGuid = ObjectMgr::GetActivePlayerGuid();
		if (playerGuid != 0 && guid != playerGuid && unit.Get<uint64>(object_fields::Owner) == playerGuid)
		{
			const bool hadTemporaryPets = HasTemporaryPets();
			m_temporaryPets.insert(guid);
			if (!hadTemporaryPets)
			{
				FrameManager::Get().TriggerLuaEvent("PET_CHANGED");
			}
		}
	}

	void PetInfo::OnUnitDespawned(const uint64 guid)
	{
		if (guid != 0 && guid == m_pet.petGuid)
		{
			m_petObservers.disconnect();
			FrameManager::Get().TriggerLuaEvent("PET_CHANGED");
			return;
		}

		if (m_temporaryPets.erase(guid) > 0 && m_temporaryPets.empty())
		{
			FrameManager::Get().TriggerLuaEvent("PET_CHANGED");
		}
	}

	void PetInfo::OnSpellCooldown(const uint64 casterGuid, const uint32 spellId, const uint32 cooldownMs)
	{
		if (casterGuid == 0 || casterGuid != m_pet.petGuid || cooldownMs == 0)
		{
			return;
		}

		Cooldown& cooldown = m_cooldowns[spellId];
		cooldown.start = GetAsyncTimeMs();
		cooldown.duration = cooldownMs;
		FrameManager::Get().TriggerLuaEvent("PET_SPELLS_CHANGED");
	}

	bool PetInfo::OnSpellFailure(const uint64 casterGuid, const uint32 spellId, const uint8 result, const char* errorKey)
	{
		if (!IsOwnPet(casterGuid))
		{
			return false;
		}

		// A failed pet cast must not keep the cooldown it may have started at cast start.
		if (casterGuid == m_pet.petGuid)
		{
			m_cooldowns.erase(spellId);
		}

		const GameTime now = GetAsyncTimeMs();
		if (spellId == m_lastFailureSpell && result == m_lastFailureResult && now - m_lastFailureTime < SpellFailureDedupeMs)
		{
			return true;
		}

		m_lastFailureSpell = spellId;
		m_lastFailureResult = result;
		m_lastFailureTime = now;

		// Interrupted casts are no error the player has to read.
		if (result != spell_cast_result::FailedInterrupted)
		{
			FirePetResult(errorKey);
		}

		return true;
	}

	const proto_client::SpellEntry* PetInfo::GetSpell(const int32 index) const
	{
		if (index < 0 || index >= static_cast<int32>(m_spells.size()))
		{
			return nullptr;
		}

		return m_spells[index];
	}

	float PetInfo::GetSpellCooldown(const int32 index) const
	{
		const proto_client::SpellEntry* spell = GetSpell(index);
		if (!spell)
		{
			return 0.0f;
		}

		const auto it = m_cooldowns.find(spell->id());
		if (it == m_cooldowns.end())
		{
			return 0.0f;
		}

		const GameTime now = GetAsyncTimeMs();
		const GameTime end = it->second.start + it->second.duration;
		if (now >= end)
		{
			return 0.0f;
		}

		return static_cast<float>(end - now) / 1000.0f;
	}

	float PetInfo::GetSpellCooldownDuration(const int32 index) const
	{
		const proto_client::SpellEntry* spell = GetSpell(index);
		if (!spell || GetSpellCooldown(index) <= 0.0f)
		{
			return 0.0f;
		}

		return static_cast<float>(m_cooldowns.at(spell->id()).duration) / 1000.0f;
	}

	void PetInfo::CastSpell(const int32 index)
	{
		if (!HasPersistentPet())
		{
			FirePetResult(GetPetResultKey(pet_result::NoPet));
			return;
		}

		const proto_client::SpellEntry* spell = GetSpell(index);
		if (!spell)
		{
			return;
		}

		if ((spell->attributes(0) & spell_attributes::Passive) != 0)
		{
			return;
		}

		if (GetSpellCooldown(index) > 0.0f)
		{
			FirePetResult("SPELL_CAST_FAILED_NOT_READY");
			return;
		}

		bool needsEnemy = false;
		bool needsAlly = false;
		bool needsAny = false;
		for (const auto& effect : spell->effects())
		{
			switch (effect.targeta())
			{
			case spell_effect_targets::TargetEnemy:
				needsEnemy = true;
				break;
			case spell_effect_targets::TargetAlly:
				needsAlly = true;
				break;
			case spell_effect_targets::TargetAny:
				needsAny = true;
				break;
			default:
				break;
			}
		}

		// The target rules of SpellCast::SetSpellTargetMap, seen from the pet: an enemy spell needs a
		// selected unit the pet is not friendly to, an ally spell falls back to the pet itself.
		SpellTargetMap targetMap;
		if (needsEnemy || needsAlly || needsAny)
		{
			const std::shared_ptr<GameUnitC> pet = ObjectMgr::Get<GameUnitC>(m_pet.petGuid);
			const std::shared_ptr<GameUnitC> selected = ObjectMgr::GetSelectedObject();

			uint64 targetGuid = 0;
			if (needsAny || (needsEnemy && needsAlly))
			{
				targetGuid = selected ? selected->GetGuid() : m_pet.petGuid;
			}
			else if (needsEnemy)
			{
				if (!selected || !selected->IsAlive() || (pet && pet->IsFriendlyTo(*selected)))
				{
					FirePetResult("SPELL_CAST_FAILED_BAD_TARGETS");
					return;
				}

				targetGuid = selected->GetGuid();
			}
			else
			{
				targetGuid = (selected && pet && pet->IsFriendlyTo(*selected)) ? selected->GetGuid() : m_pet.petGuid;
			}

			targetMap.SetTargetMap(spell_cast_target_flags::Unit);
			targetMap.SetUnitTarget(targetGuid);
		}

		m_realmConnector.PetCastSpell(spell->id(), targetMap);
	}

	void PetInfo::Attack()
	{
		const std::shared_ptr<GameUnitC> target = ObjectMgr::GetSelectedObject();
		const std::shared_ptr<GamePlayerC> player = ObjectMgr::GetActivePlayer();
		if (!target || !player || !target->IsAlive() || target->GetGuid() == player->GetGuid() || player->IsFriendlyTo(*target))
		{
			FirePetResult("PET_ERROR_NO_TARGET");
			return;
		}

		m_realmConnector.PetCommand(pet_command::Attack, target->GetGuid(), 0);
	}

	void PetInfo::Follow()
	{
		m_realmConnector.PetCommand(pet_command::Follow, 0, 0);
	}

	void PetInfo::Stay()
	{
		m_realmConnector.PetCommand(pet_command::Stay, 0, 0);
	}

	void PetInfo::SetStrategy(const uint32 strategy)
	{
		if (strategy > pet_strategy::Passive)
		{
			WLOG("Unknown pet strategy " << strategy);
			return;
		}

		m_realmConnector.PetCommand(pet_command::SetStrategy, 0, strategy);
	}

	void PetInfo::Dismiss()
	{
		m_realmConnector.PetCommand(pet_command::Dismiss, 0, 0);
	}

	void PetInfo::Rename(const String& name)
	{
		if (!HasPersistentPet())
		{
			FirePetResult(GetPetResultKey(pet_result::NoPet));
			return;
		}

		if (!m_pet.canRename)
		{
			FirePetResult(GetPetResultKey(pet_result::NamingNotAllowed));
			return;
		}

		if (!IsValidPetName(name))
		{
			FirePetResult(GetPetResultKey(pet_result::InvalidName));
			return;
		}

		m_realmConnector.PetRename(name);
	}

	PacketParseResult PetInfo::OnPetSpells(game::IncomingPacket& packet)
	{
		game::PetSpellsData data;
		if (!(packet >> data))
		{
			ELOG("Failed to read PetSpells packet");
			return PacketParseResult::Disconnect;
		}

		const uint64 previousGuid = m_pet.petGuid;
		m_pet = std::move(data);
		ObjectMgr::SetActivePetGuid(m_pet.petGuid);

		if (previousGuid != m_pet.petGuid)
		{
			m_petObservers.disconnect();
			m_cooldowns.clear();
			m_xp = 0;
			m_nextLevelXp = 0;

			// A unit counted as a temporary pet before PetSpells named it is the persistent pet.
			m_temporaryPets.erase(m_pet.petGuid);

			if (const auto unit = ObjectMgr::Get<GameUnitC>(m_pet.petGuid))
			{
				ObservePetUnit(*unit);
			}
		}

		RefreshSpells();

		if (m_pet.petGuid != 0 && !m_pet.name.empty())
		{
			m_petNames[m_pet.petGuid] = m_pet.name;
			m_pendingNameQueries.erase(m_pet.petGuid);
			ApplyPetName(m_pet.petGuid, m_pet.name);
		}

		FrameManager::Get().TriggerLuaEvent("PET_CHANGED");
		FrameManager::Get().TriggerLuaEvent("PET_SPELLS_CHANGED");
		return PacketParseResult::Pass;
	}

	PacketParseResult PetInfo::OnPetLearnedSpell(game::IncomingPacket& packet)
	{
		game::PetLearnedSpellData data;
		if (!(packet >> data))
		{
			ELOG("Failed to read PetLearnedSpell packet");
			return PacketParseResult::Disconnect;
		}

		if (m_pet.petGuid != 0 && std::find(m_pet.spells.begin(), m_pet.spells.end(), data.spellId) == m_pet.spells.end())
		{
			m_pet.spells.push_back(data.spellId);
			RefreshSpells();
			FrameManager::Get().TriggerLuaEvent("PET_SPELLS_CHANGED");
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult PetInfo::OnPetXp(game::IncomingPacket& packet)
	{
		game::PetXpData data;
		if (!(packet >> data))
		{
			ELOG("Failed to read PetXp packet");
			return PacketParseResult::Disconnect;
		}

		m_xp = data.xp;
		m_nextLevelXp = data.nextLevelXp;
		FrameManager::Get().TriggerLuaEvent("PET_XP_UPDATE");
		return PacketParseResult::Pass;
	}

	PacketParseResult PetInfo::OnPetNameQueryResult(game::IncomingPacket& packet)
	{
		game::PetNameQueryResultData data;
		if (!(packet >> data))
		{
			ELOG("Failed to read PetNameQueryResult packet");
			return PacketParseResult::Disconnect;
		}

		m_pendingNameQueries.erase(data.guid);
		if (!data.found || data.name.empty())
		{
			return PacketParseResult::Pass;
		}

		m_petNames[data.guid] = data.name;
		if (data.guid == m_pet.petGuid)
		{
			m_pet.name = data.name;
		}

		ApplyPetName(data.guid, data.name);
		return PacketParseResult::Pass;
	}

	PacketParseResult PetInfo::OnPetResult(game::IncomingPacket& packet)
	{
		game::PetResultData data;
		if (!(packet >> data))
		{
			ELOG("Failed to read PetResult packet");
			return PacketParseResult::Disconnect;
		}

		FirePetResult(GetPetResultKey(data.result));
		return PacketParseResult::Pass;
	}

	void PetInfo::RefreshSpells()
	{
		m_spells.clear();
		m_spells.reserve(m_pet.spells.size());
		for (const uint32 spellId : m_pet.spells)
		{
			if (const proto_client::SpellEntry* spell = m_spellManager.getById(spellId))
			{
				m_spells.push_back(spell);
			}
			else
			{
				WLOG("Pet knows unknown spell " << spellId);
			}
		}
	}

	void PetInfo::ObservePetUnit(GameUnitC& unit)
	{
		m_petObservers.disconnect();
		m_petObservers += unit.RegisterMirrorHandler(object_fields::MaxHealth, 2, *this, &PetInfo::OnPetHealthChanged);
		m_petObservers += unit.RegisterMirrorHandler(object_fields::Mana, 7, *this, &PetInfo::OnPetPowerChanged);
		m_petObservers += unit.RegisterMirrorHandler(object_fields::Level, 2, *this, &PetInfo::OnPetLevelChanged);
	}

	void PetInfo::ApplyPetName(const uint64 guid, const String& name)
	{
		if (const auto unit = ObjectMgr::Get<GameUnitC>(guid))
		{
			unit->SetPetName(name);
		}

		if (guid == m_pet.petGuid)
		{
			FrameManager::Get().TriggerLuaEvent("UNIT_NAME_UPDATE", "pet");
		}

		if (guid == ObjectMgr::GetSelectedObjectGuid())
		{
			FrameManager::Get().TriggerLuaEvent("UNIT_NAME_UPDATE", "target");
		}
	}

	void PetInfo::QueryPetName(const uint64 guid)
	{
		if (m_petNames.contains(guid) || !m_pendingNameQueries.insert(guid).second)
		{
			return;
		}

		m_realmConnector.PetNameQuery(guid);
	}

	bool PetInfo::IsOwnPet(const uint64 guid) const
	{
		if (guid == 0)
		{
			return false;
		}

		return guid == m_pet.petGuid || m_temporaryPets.contains(guid);
	}

	void PetInfo::OnPetHealthChanged(const uint64 monitoredGuid)
	{
		FirePetUnitEvent("UNIT_HEALTH_UPDATED");
	}

	void PetInfo::OnPetPowerChanged(const uint64 monitoredGuid)
	{
		FirePetUnitEvent("UNIT_POWER_UPDATED");
	}

	void PetInfo::OnPetLevelChanged(const uint64 monitoredGuid)
	{
		FirePetUnitEvent("UNIT_LEVEL_UPDATED");
	}
}
