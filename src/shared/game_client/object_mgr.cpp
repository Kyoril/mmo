
#include "object_mgr.h"

#include "base/macros.h"

namespace mmo
{
	std::map<uint64, std::shared_ptr<GameObjectC>> ObjectMgr::ms_objectyByGuid;
	uint64 ObjectMgr::ms_activePlayerGuid = 0;

	void ObjectMgr::Initialize()
	{
		ms_objectyByGuid.clear();
		ms_activePlayerGuid = 0;
	}

	void ObjectMgr::UpdateObjects(float deltaTime)
	{
		for (const auto& [guid, object] : ms_objectyByGuid)
		{
			object->Update(deltaTime);
		}
	}

	void ObjectMgr::AddObject(std::shared_ptr<GameObjectC> object)
	{
		ASSERT(object);
		ASSERT(!ms_objectyByGuid.contains(object->GetGuid()));
		ms_objectyByGuid[object->GetGuid()] = object;
	}

	void ObjectMgr::RemoveObject(uint64 guid)
	{
		const auto it = ms_objectyByGuid.find(guid);
		if (it != ms_objectyByGuid.end())
		{
			ms_objectyByGuid.erase(it);
		}
	}

	void ObjectMgr::SetActivePlayer(uint64 guid)
	{
		ms_activePlayerGuid = guid;
	}

	uint64 ObjectMgr::GetActivePlayerGuid()
	{
		return ms_activePlayerGuid;
	}

	std::shared_ptr<GameUnitC> ObjectMgr::GetActivePlayer()
	{
		if (ms_activePlayerGuid == 0)
		{
			return nullptr;
		}

		return Get<GameUnitC>(ms_activePlayerGuid);
	}
}
