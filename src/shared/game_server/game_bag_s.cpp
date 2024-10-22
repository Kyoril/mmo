
#include "game_bag_s.h"

#include "shared/proto_data/items.pb.h"

namespace mmo
{
	GameBagS::GameBagS(proto::Project& project, const proto::ItemEntry& entry)
		: GameItemS(project, entry)
	{
	}

	void GameBagS::Initialize()
	{
		GameItemS::Initialize();

		auto& entry = GetEntry();
		Set<uint32>(object_fields::NumSlots, entry.containerslots());
		for (uint32 slot = 0; slot < 36; slot++)
		{
			// Initialize bag slots to 0
			Set<uint64>(object_fields::Slot_1 + slot * 2, 0);
		}
	}

	bool GameBagS::IsEmpty() const
	{
		for (uint32 slot = 0; slot < 36; slot++)
		{
			if (Get<uint64>(object_fields::Slot_1 + slot * 2))
			{
				return false;
			}
		}

		return true;
	}

	io::Writer& operator<<(io::Writer& w, GameBagS const& object)
	{
		return w
			<< reinterpret_cast<GameItemS const&>(object);
	}

	io::Reader& operator>>(io::Reader& r, GameBagS& object)
	{
		return r
			>> reinterpret_cast<GameItemS&>(object);
	}
}
