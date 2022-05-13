#pragma once

#include "base/typedefs.h"

#include <memory>

#include "game/game_object_c.h"

namespace mmo
{
	class ObjectMgr
	{
	public:
		template<typename TObject>
		std::shared_ptr<TObject*> GetObject(uint64 guid)
		{
			return nullptr;
		}

	private:
		std::map<uint64, std::shared_ptr<GameObjectC>> m_objectyByGuid;
	};
}
