// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/signal.h"
#include "base/typedefs.h"
#include "client_data/project.h"
#include "game/pet_packets.h"
#include "net/realm_connector.h"

#include <map>
#include <set>
#include <vector>

struct lua_State;

namespace mmo
{
	class GameUnitC;

	/// Client side state of the active player's pets: the summoned persistent pet (from PetSpells,
	/// PetXp and PetLearnedSpell), the temporary pets in sight (units owned by the player), local pet
	/// spell cooldowns and the names of persistent pets (PetNameQueryResult). Sends the pet commands,
	/// pet casts and renames, and exposes all of it to the UI. Main thread only.
	///
	/// Lua events: PET_CHANGED (the pet, its name, strategy or the presence of pets changed),
	/// PET_SPELLS_CHANGED, PET_XP_UPDATE, PET_RESULT(localization key of an error),
	/// UNIT_HEALTH_UPDATED / UNIT_POWER_UPDATED / UNIT_LEVEL_UPDATED / UNIT_NAME_UPDATE with "pet".
	class PetInfo final : public NonCopyable
	{
	public:
		/// Two SpellFailures for the same pet spell and result within this time are shown once (the
		/// world node may send one for its own check and one from the pet's cast).
		static constexpr GameTime SpellFailureDedupeMs = 1000;

	public:
		/// Creates the pet state.
		/// @param realmConnector The connection to the realm, used to send pet packets.
		/// @param spells The spell definitions, used to resolve the pet's spells.
		explicit PetInfo(RealmConnector& realmConnector, const proto_client::SpellManager& spells);

		~PetInfo() override;

	public:
		/// Registers the pet packet handlers. Called when entering the world.
		void Initialize();

		/// Unregisters the packet handlers and forgets all pet state. Called when leaving the world.
		void Shutdown();

		/// Registers the pet functions of the UI's Lua state.
		/// @param lua The Lua state of the UI.
		void RegisterScriptFunctions(lua_State* lua);

	public:
		/// Called for every unit that spawned on the client.
		/// @param unit The new unit.
		void OnUnitSpawned(GameUnitC& unit);

		/// Called before a unit is removed from the client.
		/// @param guid The guid of the unit.
		void OnUnitDespawned(uint64 guid);

		/// Called for every SpellStart / SpellGo with a cooldown. Starts the local cooldown of a spell
		/// of the persistent pet.
		/// @param casterGuid The caster.
		/// @param spellId The spell.
		/// @param cooldownMs The cooldown in milliseconds.
		void OnSpellCooldown(uint64 casterGuid, uint32 spellId, uint32 cooldownMs);

		/// Called for every SpellFailure. A failure of one of the player's pets is reported as
		/// PET_RESULT (once, see SpellFailureDedupeMs).
		/// @param casterGuid The caster.
		/// @param spellId The spell that failed.
		/// @param result The spell_cast_result.
		/// @param errorKey The localization key of the error.
		/// @return true if the caster is one of the player's pets, so the failure is not the player's own.
		bool OnSpellFailure(uint64 casterGuid, uint32 spellId, uint8 result, const char* errorKey);

	public:
		/// Gets the guid of the summoned persistent pet, 0 if there is none.
		[[nodiscard]] uint64 GetPetGuid() const { return m_pet.petGuid; }

		/// Whether a persistent pet is summoned.
		[[nodiscard]] bool HasPersistentPet() const { return m_pet.petGuid != 0; }

		/// Whether units owned by the player other than the persistent pet are in sight.
		[[nodiscard]] bool HasTemporaryPets() const { return !m_temporaryPets.empty(); }

		/// Whether the pet UI (pet bar) should be shown: a persistent pet is summoned or temporary pets are out.
		[[nodiscard]] bool HasPetUI() const { return HasPersistentPet() || HasTemporaryPets(); }

		/// Gets the pet_strategy of the persistent pet.
		[[nodiscard]] uint8 GetStrategy() const { return m_pet.strategy; }

		/// Gets the pet_level_mode of the persistent pet.
		[[nodiscard]] uint8 GetLevelMode() const { return m_pet.levelMode; }

		/// Gets the name of the persistent pet.
		[[nodiscard]] const String& GetPetName() const { return m_pet.name; }

		/// Whether the owner may rename the persistent pet.
		[[nodiscard]] bool CanRename() const { return HasPersistentPet() && m_pet.canRename; }

		/// Gets the persistent pet's experience towards its next level.
		[[nodiscard]] uint32 GetXp() const { return m_xp; }

		/// Gets the experience the persistent pet needs for its next level.
		[[nodiscard]] uint32 GetNextLevelXp() const { return m_nextLevelXp; }

		/// Gets the number of spells of the persistent pet (passive spells included).
		[[nodiscard]] uint32 GetNumSpells() const { return static_cast<uint32>(m_spells.size()); }

		/// Gets a spell of the persistent pet.
		/// @param index Zero based index, below GetNumSpells.
		/// @return The spell, or nullptr if the index is out of range.
		[[nodiscard]] const proto_client::SpellEntry* GetSpell(int32 index) const;

		/// Gets the remaining local cooldown of a spell of the persistent pet.
		/// @param index Zero based index, below GetNumSpells.
		/// @return Remaining cooldown in seconds, 0 if it is ready.
		[[nodiscard]] float GetSpellCooldown(int32 index) const;

		/// Gets the full cooldown duration of the last cooldown started for a spell of the persistent pet.
		/// @param index Zero based index, below GetNumSpells.
		/// @return Cooldown duration in seconds, 0 if there is no cooldown running.
		[[nodiscard]] float GetSpellCooldownDuration(int32 index) const;

	public:
		/// Makes the persistent pet cast one of its spells at the current target (like CastSpell).
		/// @param index Zero based index, below GetNumSpells.
		void CastSpell(int32 index);

		/// Sends all pets to attack the current target. Without a target the player can attack, PET_RESULT
		/// fires with PET_ERROR_NO_TARGET (the world node does not answer an attack on an unknown target).
		void Attack();

		/// Makes all pets follow the player.
		void Follow();

		/// Makes all pets stay where they are.
		void Stay();

		/// Sets the strategy of all pets.
		/// @param strategy The pet_strategy.
		void SetStrategy(uint32 strategy);

		/// Dismisses all pets.
		void Dismiss();

		/// Renames the persistent pet. Invalid names and pets that cannot be renamed fire PET_RESULT locally.
		/// @param name The new name.
		void Rename(const String& name);

	private:
		PacketParseResult OnPetSpells(game::IncomingPacket& packet);
		PacketParseResult OnPetLearnedSpell(game::IncomingPacket& packet);
		PacketParseResult OnPetXp(game::IncomingPacket& packet);
		PacketParseResult OnPetNameQueryResult(game::IncomingPacket& packet);
		PacketParseResult OnPetResult(game::IncomingPacket& packet);

	private:
		/// Rebuilds m_spells from the pet's spell ids.
		void RefreshSpells();

		/// Watches the persistent pet's unit for health, power and level changes.
		void ObservePetUnit(GameUnitC& unit);

		/// Applies a known pet name to the unit with the guid, if it is in sight, and fires UNIT_NAME_UPDATE.
		void ApplyPetName(uint64 guid, const String& name);

		/// Asks the world node for the name of a persistent pet, unless it is known or already asked for.
		void QueryPetName(uint64 guid);

		/// Whether the unit with this guid is a pet of the player (persistent or temporary).
		[[nodiscard]] bool IsOwnPet(uint64 guid) const;

		void OnPetHealthChanged(uint64 monitoredGuid);
		void OnPetPowerChanged(uint64 monitoredGuid);
		void OnPetLevelChanged(uint64 monitoredGuid);

	private:
		/// A running local cooldown of a pet spell.
		struct Cooldown
		{
			GameTime start = 0;
			GameTime duration = 0;
		};

		RealmConnector& m_realmConnector;
		const proto_client::SpellManager& m_spellManager;
		RealmConnector::PacketHandlerHandleContainer m_packetHandlerHandles;

		/// The last PetSpells (petGuid 0 = no persistent pet).
		game::PetSpellsData m_pet;
		/// The pet's spells resolved from m_pet.spells, in the same order (unknown ids skipped).
		std::vector<const proto_client::SpellEntry*> m_spells;
		uint32 m_xp = 0;
		uint32 m_nextLevelXp = 0;

		/// Units in sight owned by the player, apart from the persistent pet.
		std::set<uint64> m_temporaryPets;
		/// Local cooldowns of the persistent pet's spells by spell id.
		std::map<uint32, Cooldown> m_cooldowns;
		/// Known names of persistent pets by guid.
		std::map<uint64, String> m_petNames;
		/// Persistent pets whose name was asked for and not answered yet.
		std::set<uint64> m_pendingNameQueries;
		/// Health, power and level observers of the persistent pet's unit.
		scoped_connection_container m_petObservers;

		/// The last pet spell failure shown (for SpellFailureDedupeMs).
		uint32 m_lastFailureSpell = 0;
		uint8 m_lastFailureResult = 0;
		GameTime m_lastFailureTime = 0;
	};
}
