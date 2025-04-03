
#include "game_world_object_s_base.h"

#include "shared/proto_data/objects.pb.h"

namespace mmo
{
	GameWorldObjectS_Base::GameWorldObjectS_Base(const proto::Project& project, const proto::ObjectEntry& entry)
		: GameObjectS(project)
		, m_entry(entry)
	{
	}

	void GameWorldObjectS_Base::Initialize()
	{
		GameObjectS::Initialize();

		Set<uint32>(object_fields::Entry, m_entry.id());
	}

	const String& GameWorldObjectS_Base::GetName() const
	{
		return m_entry.name();
	}

	void GameWorldObjectS_Base::PrepareFieldMap()
	{
		m_fields.Initialize(object_fields::WorldObjectFieldCount);
	}

	GameWorldObjectS_Chest::GameWorldObjectS_Chest(const proto::Project& project, const proto::ObjectEntry& entry)
		: GameWorldObjectS_Base(project, entry)
	{
	}

	void GameWorldObjectS_Chest::Initialize()
	{
		GameWorldObjectS_Base::Initialize();

	}
}
