#pragma once

#include "base/typedefs.h"

#include <memory>

#include "game/game_object_c.h"
#include "game/game_unit_c.h"

namespace mmo
{
	class ObjectMgr
	{
	public:
		static void Initialize();

		template<typename TObject>
		static std::shared_ptr<TObject> Get(uint64 guid)
		{
			if (guid == 0)
			{
				return nullptr;
			}

			if (const auto it = ms_objectyByGuid.find(guid); it != ms_objectyByGuid.end())
			{
				return std::dynamic_pointer_cast<TObject>(it->second);
			}

			return nullptr;
		}

		static void UpdateObjects(float deltaTime);

		static void AddObject(std::shared_ptr<GameObjectC> object);

		static void RemoveObject(uint64 guid);

		static void SetActivePlayer(uint64 guid);

		static uint64 GetActivePlayerGuid();

		static std::shared_ptr<GameUnitC> GetActivePlayer();

	private:
		static std::map<uint64, std::shared_ptr<GameObjectC>> ms_objectyByGuid;
		static uint64 ms_activePlayerGuid;
	};
}
