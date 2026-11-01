// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "realm_module.h"

#include "database.h"
#include "game_server/pets/persistent_pet.h"

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace mmo
{
	/// Persistent pets, realm side: owns the character_pets and character_pet_spells tables.
	///
	/// A character's stored pets are loaded before it joins its world node and handed to the node
	/// right after the join (realm_world_packet::PlayerPetData). While the character is in the world
	/// the node owns the live copy and reports every change (world_realm_packet::PetData, with each
	/// character save and whenever the pets change), which is saved; new pets (granted or tamed) are
	/// created here, because the database assigns their ids (CreatePet / PetCreated).
	///
	/// The module keeps the last known pets of each character in the world, keyed by character guid.
	/// They are what a world node gets on the next join (map transfer), and what tells which pets a
	/// save leaves out (removed on the world node, a GM command for example) so their rows are deleted.
	///
	/// Every database request is keyed by the character, so its loads, saves and creations run in the
	/// order they were made.
	///
	/// The PETS subsystem is owned by the world node, which refuses pet actions while it is off. Pet
	/// data is still loaded and saved here, so switching it off never loses pet progress.
	class PetModule final : public RealmModule
	{
	public:
		/// Called with the created pet: the request with its new database id, or with id 0 if the pet
		/// could not be created.
		typedef std::function<void(const PersistentPetData&)> CreatedCallback;

	public:
		explicit PetModule(AsyncPetDatabase& database);

	public:
		[[nodiscard]] const char* GetName() const override { return "Pets"; }

		[[nodiscard]] std::optional<game::Subsystem> GetSubsystem() const override { return game::subsystem::Pets; }

		void OnCharacterLoading(Player& player, uint64 characterGuid, std::function<void()> loaded) override;

		/// Sends the character's pets to the world node it joins, if they could be loaded.
		void OnJoiningWorld(Player& player, World& world) override;

		void OnLeftWorld(Player& player) override;

		void OnCharacterDeleting(Player& player, uint64 characterGuid) override;

	public:
		/// Loads the stored pets of a character and keeps them. @p loaded is called once the load
		/// finished, also when it failed: the character then enters the world without pets, and
		/// GetPetsForJoin hands nothing to the world node, which therefore never saves any.
		void LoadPets(uint64 characterGuid, std::function<void()> loaded);

		/// @return The last known stored pets of a character, or nullptr if they are not loaded.
		[[nodiscard]] const std::vector<PersistentPetData>* FindPets(uint64 characterGuid) const;

		/// The pets to hand to a world node the character joins. From then on the node knows each of
		/// them, so a save of the node that leaves one out removes it.
		/// @return The pets, or nullopt if they are not loaded (nothing must be sent then).
		std::optional<std::vector<PersistentPetData>> GetPetsForJoin(uint64 characterGuid);

		/// Forgets the pets of a character that left the world. A save that arrives later (the world
		/// node's last one after a disconnect) is still written, but removes no pets.
		void ForgetPets(uint64 characterGuid);

		/// Saves the stored pets a world node reported (PetData). Only pets that changed are written.
		/// A pet the node no longer reports is deleted, unless it was created after the node got its
		/// list (it may not have heard of it yet).
		void SavePets(uint64 characterGuid, std::vector<PersistentPetData> pets);

		/// Creates a stored pet (CreatePet). The request is refused (answered with id 0) when the
		/// character has MaxStoredPets pets already.
		/// @param request The pet to create; every field but the id is echoed back unchanged.
		/// @param created Called with the result, on the io thread.
		void CreatePet(uint64 characterGuid, const PersistentPetData& request, CreatedCallback created);

		/// Deletes every stored pet of a character (character deletion).
		void DeletePets(uint64 characterGuid);

	private:
		/// The last known stored pets of a character in the world.
		struct CharacterPets
		{
			std::vector<PersistentPetData> pets;

			/// Pets created since the world node got its list, which it may not know about yet. A
			/// save that leaves them out does not remove them.
			std::set<uint64> unknownToWorld;
		};

	private:
		AsyncPetDatabase& m_database;
		std::map<uint64, CharacterPets> m_characters;
	};
}
