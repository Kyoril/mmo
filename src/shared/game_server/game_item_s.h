#pragma once

#include "game_object_s.h"

namespace mmo
{
	namespace proto
	{
		class SpellEntry;
		class ItemEntry;
	}

	/// @brief Represents an item instance in a world..
	class GameItemS : public GameObjectS
	{
		signal<void()> equipped;

	public:
		explicit GameItemS(const proto::Project& project, const proto::ItemEntry& entry);

		~GameItemS() override = default;

		virtual void Initialize() override;

		ObjectTypeId GetTypeId() const override { return ObjectTypeId::Item; }

		const proto::ItemEntry& GetEntry() const { return m_entry; }

		uint32 GetStackCount() const
		{
			return Get<uint32>(object_fields::StackCount);
		}

		uint16 AddStacks(uint16 amount);

		void NotifyEquipped();

		bool IsBroken() const
		{
			return Get<uint32>(object_fields::MaxDurability) > 0 && Get<uint32>(object_fields::Durability) == 0;
		}

		bool IsCompatibleWithSpell(const proto::SpellEntry& spell);

	protected:
		void PrepareFieldMap() override
		{
			m_fields.Initialize(object_fields::ItemFieldCount);
		}

	private:
		friend io::Writer& operator << (io::Writer& w, GameItemS const& object);
		friend io::Reader& operator >> (io::Reader& r, GameItemS& object);

	private:
		const proto::ItemEntry& m_entry;
	};

	io::Writer& operator<<(io::Writer& w, GameItemS const& object);

	io::Reader& operator>> (io::Reader& r, GameItemS& object);
}