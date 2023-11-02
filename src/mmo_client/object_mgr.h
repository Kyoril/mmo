#pragma once

#include "base/typedefs.h"

#include <memory>

#include "game/game_object_c.h"

namespace mmo
{
	class ObjectMgr
	{
	public:
		void Initialize();

		template<typename TObject>
		std::shared_ptr<TObject> GetObject(uint64 guid)
		{
			if (const auto it = m_objectyByGuid.find(guid); it != m_objectyByGuid.end())
			{
				return std::dynamic_pointer_cast<TObject>(it->second);
			}
		}

		void AddObject(std::shared_ptr<GameObjectC> object);

		void RemoveObject(uint64 guid);

	private:
		std::map<uint64, std::shared_ptr<GameObjectC>> m_objectyByGuid;
	};
}
