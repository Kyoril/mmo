
#include "game_bag_c.h"

#include "game_unit_c.h"

namespace mmo
{
	GameBagC::GameBagC(Scene& scene, NetClient& netDriver)
		: GameItemC(scene, netDriver)
	{
	}

	void GameBagC::Deserialize(io::Reader& reader, bool complete)
	{
		GameItemC::Deserialize(reader, complete);
	}

	void GameBagC::InitializeFieldMap()
	{
		m_fieldMap.Initialize(object_fields::BagFieldCount);
	}
}
