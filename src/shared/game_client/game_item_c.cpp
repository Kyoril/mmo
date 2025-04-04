
#include "game_item_c.h"

#include "game_unit_c.h"
#include "net_client.h"
#include "client_data/project.h"
#include "shared/client_data/proto_client/item_display.pb.h"

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

	const proto_client::ItemDisplayEntry* GameItemC::GetDisplayData() const
	{
		if (!m_info) return nullptr;

		return m_project.itemDisplays.getById(m_info->displayId);
	}
}
