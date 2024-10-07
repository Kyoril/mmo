#pragma once

#include "base/typedefs.h"

#include <memory>

#include "game_client/game_object_c.h"
#include "game_client/game_unit_c.h"

namespace mmo
{
	namespace proto_client
	{
		class ModelDataEntry;
		class Project;
	}

	class ObjectMgr
	{
	public:
		static void Initialize(const proto_client::Project& project);

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

		static const proto_client::ModelDataEntry* GetModelData(uint32 displayId);

	private:
		static std::map<uint64, std::shared_ptr<GameObjectC>> ms_objectyByGuid;
		static uint64 ms_activePlayerGuid;
		static const proto_client::Project* ms_project;
	};
}
