
#include "game_item_c.h"

#include "game_unit_c.h"

namespace mmo
{
	GameItemC::GameItemC(Scene& scene, NetClient& netDriver, const proto_client::Project& project)
		: GameObjectC(scene, project)
		, m_netDriver(netDriver)
	{
	}

	void GameItemC::Deserialize(io::Reader& reader, bool complete)
	{
		GameObjectC::Deserialize(reader, complete);

		m_netDriver.GetItemData(Get<uint32>(object_fields::Entry), std::static_pointer_cast<GameItemC>(shared_from_this()));
	}

	void GameItemC::InitializeFieldMap()
	{
		m_fieldMap.Initialize(object_fields::ItemFieldCount);
	}
}
