// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "game_object_s.h"

namespace mmo
{
	namespace proto
	{
		class SpellEntry;
		class ItemEntry;
	}

	/// Represents an item instance in the game world.
	/// Extends GameObjectS to provide item-specific functionality.
	class GameItemS : public GameObjectS
	{
		/// Signal that is triggered when the item is equipped by a character.
		signal<void()> equipped;

	public:
		/// Constructs a new item object from project and item entry data.
		/// @param project The project containing game configuration.
		/// @param entry The specific item entry data for this item.
		explicit GameItemS(const proto::Project& project, const proto::ItemEntry& entry);

		/// Default destructor.
		~GameItemS() override = default;

		/// Initializes the item object, setting up its initial state.
		virtual void Initialize() override;

		/// Gets the type identifier for this object.
		/// @returns ObjectTypeId::Item as the type identifier for items.
		ObjectTypeId GetTypeId() const override { return ObjectTypeId::Item; }

		/// Gets the item entry data that defines this item's properties.
		/// @returns Constant reference to the item entry.
		const proto::ItemEntry& GetEntry() const { return m_entry; }

		/// Gets the current number of items in this stack.
		/// @returns The stack count as a 32-bit unsigned integer.
		uint32 GetStackCount() const
		{
			return Get<uint32>(object_fields::StackCount);
		}

		/// Adds a specified amount of items to the stack.
		/// @param amount The number of items to add to the stack.
		/// @returns The actual number of items added as a 16-bit unsigned integer.
		uint16 AddStacks(uint16 amount);

		/// Triggers the equipped signal to notify listeners that the item was equipped.
		void NotifyEquipped();

		/// Checks if the item is broken (has zero durability).
		/// @returns true if the item has maximum durability greater than zero and current durability equal to zero.
		bool IsBroken() const
		{
			return Get<uint32>(object_fields::MaxDurability) > 0 && Get<uint32>(object_fields::Durability) == 0;
		}

		/// Checks if the item is compatible with a specified spell.
		/// @param spell The spell entry to check compatibility with.
		/// @returns true if the item is compatible with the spell, false otherwise.
		bool IsCompatibleWithSpell(const proto::SpellEntry& spell);

		/// Gets the name of the item.
		/// @returns The item name as a constant reference to a String.
		const String& GetName() const override;

	protected:
		/// Prepares the field map for storing item properties.
		/// Initializes the fields with the appropriate size for item objects.
		void PrepareFieldMap() override
		{
			m_fields.Initialize(object_fields::ItemFieldCount);
		}

	private:
		friend io::Writer& operator << (io::Writer& w, GameItemS const& object);
		friend io::Reader& operator >> (io::Reader& r, GameItemS& object);

	private:
		/// Reference to the item entry data that defines this item's properties.
		const proto::ItemEntry& m_entry;
	};

	/// Serializes a GameItemS object to a Writer for binary serialization.
	/// Used to convert the object state into a binary format for storage or network transmission.
	/// @param w The Writer to write to.
	/// @param object The GameItemS object to serialize.
	/// @returns Reference to the Writer for chaining.
	io::Writer& operator<<(io::Writer& w, GameItemS const& object);

	/// Deserializes a GameItemS object from a Reader during binary deserialization.
	/// Used to reconstruct the object state from a binary format after storage or network transmission.
	/// @param r The Reader to read from.
	/// @param object The GameItemS object to deserialize into.
	/// @returns Reference to the Reader for chaining.
	io::Reader& operator>> (io::Reader& r, GameItemS& object);
}