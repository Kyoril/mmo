
#include "game_item_s.h"

#include "game_server/spells/spell_cast.h"
#include "shared/proto_data/items.pb.h"

namespace mmo
{
	GameItemS::GameItemS(const proto::Project& project, const proto::ItemEntry& entry)
		: GameObjectS(project)
		, m_entry(entry)
	{
	}

	void GameItemS::Initialize()
	{
		GameObjectS::Initialize();

		Set<uint32>(object_fields::Entry, m_entry.id(), false);
		Set<float>(object_fields::Scale, 1.0f, false);
		Set<uint32>(object_fields::MaxDurability, m_entry.durability(), false);
		Set<uint32>(object_fields::Durability, m_entry.durability(), false);
		Set<uint32>(object_fields::StackCount, 1, false);
		Set<uint32>(object_fields::ItemFlags, m_entry.flags(), false);

		// Spell charges
		for (uint8 i = 0; i < 5; ++i)
		{
			if (i >= m_entry.spells_size())
			{
				break;
			}

			Set<uint32>(object_fields::SpellCharges + i, 0, false);
		}
	}

	uint16 GameItemS::AddStacks(uint16 amount)
	{
		const uint32 stackCount = GetStackCount();

		const uint32 availableStacks = m_entry.maxstack() - stackCount;
		if (amount <= availableStacks)
		{
			Set<uint32>(object_fields::StackCount, stackCount + amount);
			return amount;
		}

		// Max out the stack count and return the added stacks
		Set<uint32>(object_fields::StackCount, m_entry.maxstack());
		return static_cast<uint16>(availableStacks);
	}

	void GameItemS::NotifyEquipped()
	{
		equipped();
	}

	bool GameItemS::IsCompatibleWithSpell(const proto::SpellEntry& spell)
	{
		if (spell.itemclass() != -1)
		{
			if (spell.itemclass() != static_cast<int32>(m_entry.itemclass()))
			{
				return false;
			}

			if (spell.itemsubclassmask() != 0 &&
				(spell.itemsubclassmask() & (1 << m_entry.subclass())) == 0)
			{
				return false;
			}
		}

		return true;
	}

	const String& GameItemS::GetName() const
	{
		return m_entry.name();
	}

	io::Writer& operator<<(io::Writer& w, GameItemS const& object)
	{
		w << reinterpret_cast<GameObjectS const&>(object);
		return w;
	}

	io::Reader& operator>>(io::Reader& r, GameItemS& object)
	{
		r >> reinterpret_cast<GameObjectS&>(object);
		return r;
	}
}
