#pragma once

#include "base/typedefs.h"

#include <memory>

#include "movement.h"
#include "game_client/game_object_c.h"
#include "game_client/game_player_c.h"

namespace mmo
{
	class UnitHandle;
	class PartyInfo;

	namespace proto_client
	{
		class ModelDataEntry;
		class Project;
	}

	class ObjectMgr
	{
	public:
		static void Initialize(const proto_client::Project& project, PartyInfo& partyInfo);

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

		static void RemoveAllObjects();

		static void SetActivePlayer(uint64 guid);

		static uint64 GetActivePlayerGuid();

		static uint64 GetSelectedObjectGuid();

		static void SetSelectedObjectGuid(uint64 guid);

		static uint32 GetItemCount(uint32 itemId);

		static std::shared_ptr<GamePlayerC> GetActivePlayer();

		static const proto_client::ModelDataEntry* GetModelData(uint32 displayId);

		static MovementGlobals& GetMovementGlobals() { return ms_movementGlobals; }

		static std::shared_ptr<UnitHandle> GetUnitHandleByName(const std::string& unitName);

	private:
		static void OnItemStackCountChanged(uint64 itemGuid);

	public:

		template<typename U, typename T>
		static void ForEachObject(T callback)
		{
			for (const auto& [guid, object] : ms_objectyByGuid)
			{
				if (std::shared_ptr<U> unit = std::dynamic_pointer_cast<U>(object))
				{
					callback(unit);
				}
			}
		}

	private:
		static std::map<uint64, std::shared_ptr<GameObjectC>> ms_objectyByGuid;
		static uint64 ms_activePlayerGuid;
		static uint64 ms_selectedObjectGuid;
		static const proto_client::Project* ms_project;

		static std::map<uint32, uint32> ms_itemCount;
		static std::map<uint64, scoped_connection> ms_itemConnections;

		static MovementGlobals ms_movementGlobals;

		static PartyInfo* ms_partyInfo;
	};
}
