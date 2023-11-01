
#include "object_mgr.h"

#include "base/macros.h"

namespace mmo
{
	void ObjectMgr::Initialize()
	{
		m_objectyByGuid.clear();
	}

	void ObjectMgr::AddObject(std::shared_ptr<GameObjectC> object)
	{
		ASSERT(object);
		m_objectyByGuid[object->GetGuid()] = object;
	}

	void ObjectMgr::RemoveObject(uint64 guid)
	{
		const auto it = m_objectyByGuid.find(guid);
		if (it != m_objectyByGuid.end())
		{
			m_objectyByGuid.erase(it);
		}
	}
}
