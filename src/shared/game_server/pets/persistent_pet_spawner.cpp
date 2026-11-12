// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "persistent_pet_spawner.h"

#include "base/utilities.h"
#include "game/guid_utils.h"
#include "game/pet_defines.h"
#include "game_server/ai/creature_ai.h"
#include "game_server/objects/game_creature_s.h"
#include "game_server/objects/game_player_s.h"
#include "game_server/pets/pet_manager.h"
#include "game_server/spells/spell_effects.h"
#include "game_server/world/world_instance.h"
#include "game/spell_target_map.h"
#include "log/default_log_levels.h"
#include "shared/proto_data/project.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace mmo
{
	const proto::UnitEntry* GetPersistentPetUnitEntry(const proto::Project& project, const PersistentPetData& data, const proto::PetEntry& entry)
	{
		return project.units.getById(data.unitEntry != 0 ? data.unitEntry : entry.unit_entry());
	}

	uint32 GetPetMasterLevel(const GamePlayerS& owner, const proto::PetEntry& entry)
	{
		if (static_cast<pet_level_source::Type>(entry.level_source()) == pet_level_source::ClassLevel)
		{
			return std::max<uint32>(1, owner.GetActiveClassLevel());
		}

		// CharacterLevel, and Creature which has no meaning for a persistent pet.
		return std::max<uint32>(1, owner.GetLevel());
	}

	uint32 ResolvePersistentPetLevel(const GamePlayerS& owner, const PersistentPetData& data, const proto::PetEntry& entry)
	{
		const uint32 masterLevel = GetPetMasterLevel(owner, entry);
		if (static_cast<pet_level_mode::Type>(entry.level_mode()) == pet_level_mode::MasterLevel)
		{
			return masterLevel;
		}

		return std::clamp<uint32>(data.level, 1, masterLevel);
	}

	std::vector<uint32> GetKnownPetSpells(const proto::PetEntry& entry, const PersistentPetData& data, const uint32 level)
	{
		std::vector<uint32> spells;
		const auto add = [&spells](const uint32 spellId)
		{
			if (spellId != 0 && std::find(spells.begin(), spells.end(), spellId) == spells.end())
			{
				spells.push_back(spellId);
			}
		};

		for (const uint32 spellId : entry.default_spells())
		{
			add(spellId);
		}

		for (const uint32 spellId : data.spells)
		{
			add(spellId);
		}

		for (const auto& levelSpell : entry.level_spells())
		{
			if (levelSpell.level() <= level)
			{
				add(levelSpell.spell());
			}
		}

		return spells;
	}

	std::vector<proto::UnitSpellEntry> BuildPetCombatSpells(const proto::PetEntry& entry, const std::vector<uint32>& spells)
	{
		std::vector<proto::UnitSpellEntry> result;
		result.reserve(spells.size());

		for (const uint32 spellId : spells)
		{
			const auto tuning = std::find_if(entry.ai_spells().begin(), entry.ai_spells().end(), [spellId](const proto::UnitSpellEntry& aiSpell)
				{
					return aiSpell.spellid() == spellId;
				});

			if (tuning != entry.ai_spells().end())
			{
				result.push_back(*tuning);
				continue;
			}

			// The UnitSpellEntry defaults: normal priority, the spell's own cooldown and range.
			proto::UnitSpellEntry defaults;
			defaults.set_spellid(spellId);
			result.push_back(std::move(defaults));
		}

		return result;
	}

	String ChoosePetName(const proto::PetEntry& entry, const String& tamedCreatureName)
	{
		if (static_cast<pet_name_mode::Type>(entry.name_mode()) == pet_name_mode::RandomFromList && entry.random_names_size() > 0)
		{
			std::uniform_int_distribution<int> nameDist(0, entry.random_names_size() - 1);
			return ClampStoredPetName(entry.random_names(nameDist(randomGenerator)));
		}

		// The realm stores at most MaxStoredPetNameBytes, and a creature name can be longer.
		return ClampStoredPetName(tamedCreatureName.empty() ? entry.name() : tamedCreatureName);
	}

	uint32 GetPetXpToNextLevel(const proto::ClassEntry* classEntry, const uint32 level)
	{
		// The same curve as GamePlayerS::SetLevel uses for the character.
		if (!classEntry || classEntry->xptonextlevel_size() == 0)
		{
			return 400 * std::max<uint32>(1, level);
		}

		const int32 index = std::min(static_cast<int32>(std::max<uint32>(1, level)) - 1, classEntry->xptonextlevel_size() - 1);
		return std::max<uint32>(1, classEntry->xptonextlevel(index));
	}

	std::shared_ptr<GameCreatureS> SpawnPersistentPet(GamePlayerS& owner, WorldInstance& world, const PersistentPetData& data,
		const proto::PetEntry& entry, const proto::UnitEntry& unitEntry, const uint32 healthPercent)
	{
		const Vector3 position = SummonPositions(owner.GetPosition(), owner.GetFacing(), 1, DefaultSummonDistance).front();
		const float facing = owner.GetFacing().GetValueRadians();

		auto pet = world.CreateTemporaryCreature(unitEntry, position, facing, 0.0f, MakePetGuid(data.dbId, data.petEntry));
		if (!pet)
		{
			return nullptr;
		}

		// Unlike a temporary pet, a persistent pet keeps GrantsKillRewards: being a pet is what
		// keeps it from rewarding its killer.
		pet->SetSummonOwner(std::static_pointer_cast<GameUnitS>(owner.shared_from_this()));
		pet->SetOwnerGuid(owner.GetGuid());
		pet->SetPetGroup(0);
		pet->SetCustomName(data.name);

		// A tamed pet keeps the model of the creature it was tamed from.
		if (data.unitEntry == 0 && entry.display_id() != 0)
		{
			pet->Set<uint32>(object_fields::DisplayId, entry.display_id());
		}

		pet->SetPetStatMultipliers(entry.health_multiplier(), entry.mana_multiplier(), entry.damage_multiplier(), entry.armor_multiplier());
		pet->SetPetOwnerStatMultipliers(owner.GetPetStatMultiplier(pet_stat_kind::Health), owner.GetPetStatMultiplier(pet_stat_kind::Damage));

		// SetLevel refreshes the stats (with the multipliers) and fills health and power up.
		const uint32 level = ResolvePersistentPetLevel(owner, data, entry);
		pet->SetLevel(level);

		const uint32 powerType = pet->GetPowerType();
		if (healthPercent > 0)
		{
			const uint32 health = static_cast<uint32>(static_cast<uint64>(pet->GetMaxHealth()) * std::min<uint32>(healthPercent, 100) / 100);
			pet->Set<uint32>(object_fields::Health, std::max<uint32>(1, health));
		}
		else if (data.health > 0)
		{
			// Health 0 is a fresh pet, which keeps the full health and power SetLevel gave it.
			pet->Set<uint32>(object_fields::Health, std::min(data.health, pet->GetMaxHealth()));
			pet->Set<uint32>(object_fields::Mana + powerType, std::min(data.power, pet->GetMaxPower()));
		}

		pet->ClearFieldChanges();
		world.AddGameObject(*pet);
		owner.GetPets().SetPersistentPet(pet.get());

		if (CreatureAI* ai = pet->GetAI())
		{
			ai->EnterPetMode(owner);
		}

		ApplyPersistentPetSpells(*pet, entry, GetKnownPetSpells(entry, data, level));

		// The owner's PetPassive auras put their passive spells on every pet it summons.
		const std::vector<uint32> passiveSpells = owner.GetPetPassiveSpells();
		for (const uint32 spellId : passiveSpells)
		{
			ApplyPetPassiveSpell(*pet, spellId);
		}

		return pet;
	}

	void ApplyPersistentPetSpells(GameCreatureS& pet, const proto::PetEntry& entry, const std::vector<uint32>& spells)
	{
		for (const uint32 spellId : spells)
		{
			if (!pet.HasSpell(spellId))
			{
				pet.AddSpell(spellId);
			}
		}

		if (CreatureAI* ai = pet.GetAI())
		{
			ai->SetPetSpells(BuildPetCombatSpells(entry, spells));
		}
	}

	void RefreshPersistentPetOwnerStats(GameCreatureS& pet, const GameUnitS& owner)
	{
		const uint32 oldMaxHealth = pet.GetMaxHealth();
		const uint32 oldHealth = pet.GetHealth();

		pet.SetPetOwnerStatMultipliers(owner.GetPetStatMultiplier(pet_stat_kind::Health), owner.GetPetStatMultiplier(pet_stat_kind::Damage));
		pet.RefreshStats();

		// A dead pet stays dead; a living one keeps its share of health.
		if (!pet.IsAlive() || oldMaxHealth == 0)
		{
			return;
		}

		const uint32 newMaxHealth = pet.GetMaxHealth();
		const uint32 health = static_cast<uint32>(std::round(static_cast<double>(oldHealth) * newMaxHealth / oldMaxHealth));
		pet.Set<uint32>(object_fields::Health, std::clamp<uint32>(health, 1, newMaxHealth));
	}

	void ApplyPetPassiveSpell(GameCreatureS& pet, const uint32 spellId)
	{
		const proto::SpellEntry* spell = pet.GetProject().spells.getById(spellId);
		if (!spell)
		{
			WLOG("PetPassive references unknown spell " << spellId);
			return;
		}

		if (pet.HasAuraSpellFromCaster(spellId, pet.GetGuid()))
		{
			return;
		}

		SpellTargetMap targetMap;
		targetMap.SetTargetMap(spell_cast_target_flags::Self);
		pet.CastSpell(targetMap, *spell, 0, true, 0, true);
	}

	void RemovePetPassiveSpell(GameCreatureS& pet, const uint32 spellId)
	{
		pet.RemoveAllAurasFromCaster(pet.GetGuid(), spellId);
	}

	void StorePersistentPetState(const GameCreatureS& pet, const proto::PetEntry& entry, PersistentPetData& data)
	{
		if (!pet.IsAlive())
		{
			return;
		}

		data.health = std::max<uint32>(1, pet.GetHealth());
		data.power = pet.GetPower();

		// An OwnLevel pet's stored level is authoritative (the pet may be summoned capped below it).
		if (static_cast<pet_level_mode::Type>(entry.level_mode()) == pet_level_mode::MasterLevel)
		{
			data.level = pet.GetLevel();
		}
	}
}
