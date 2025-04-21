// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "game_item_s.h"

namespace mmo
{
	/// A game container that can hold items in slots.
	/// Inherits from GameItemS as bags themselves are also items.
	class GameBagS final : public GameItemS
	{
		friend io::Writer& operator << (io::Writer& w, GameBagS const& object);
		friend io::Reader& operator >> (io::Reader& r, GameBagS& object);

	public:
		/// Constructs a new bag object from project and item entry data.
		/// @param project The project containing game configuration.
		/// @param entry The specific item entry data for this bag.
		explicit GameBagS(const proto::Project& project, const proto::ItemEntry& entry);
		
		/// Default destructor.
		~GameBagS() override = default;

	public:
		/// Initializes the bag object, setting up its initial state.
		void Initialize() override;

		/// Gets the type identifier for this object.
		/// @returns ObjectTypeId::Container as the type identifier for bags.
		ObjectTypeId GetTypeId() const override { return ObjectTypeId::Container; }

		/// Gets the number of slots available in this bag.
		/// @returns The number of slots as a 32-bit unsigned integer.
		uint32 GetSlotCount() const { return Get<uint32>(object_fields::NumSlots); }

		/// Gets the number of free slots available in this bag.
		/// @returns The number of free slots as a 32-bit unsigned integer.
		uint32 GetFreeSlots() const;

		/// Checks if the bag has no items in it.
		/// @returns true if the bag contains no items, false otherwise.
		bool IsEmpty() const;

	protected:
		/// Prepares the field map for storing bag properties.
		/// Initializes the fields with the appropriate size for bag objects.
		void PrepareFieldMap() override
		{
			m_fields.Initialize(object_fields::BagFieldCount);
		}
	};

	/// Serializes a GameBagS object to a Writer for binary serialization.
	/// Used to convert the object state into a binary format for storage or network transmission.
	/// @param w The Writer to write to.
	/// @param object The GameBagS object to serialize.
	/// @returns Reference to the Writer for chaining.
	io::Writer& operator << (io::Writer& w, GameBagS const& object);
	
	/// Deserializes a GameBagS object from a Reader during binary deserialization.
	/// Used to reconstruct the object state from a binary format after storage or network transmission.
	/// @param r The Reader to read from.
	/// @param object The GameBagS object to deserialize into.
	/// @returns Reference to the Reader for chaining.
	io::Reader& operator >> (io::Reader& r, GameBagS& object);
}