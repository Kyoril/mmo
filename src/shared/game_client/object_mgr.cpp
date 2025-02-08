
#include "object_mgr.h"

#include "game_item_c.h"
#include "base/macros.h"
#include "client_data/project.h"

namespace mmo
{
	MovementGlobals ObjectMgr::ms_movementGlobals;
	std::map<uint64, std::shared_ptr<GameObjectC>> ObjectMgr::ms_objectyByGuid;
	uint64 ObjectMgr::ms_activePlayerGuid = 0;
	const proto_client::Project* ObjectMgr::ms_project = nullptr;

	std::map<uint32, uint32> ObjectMgr::ms_itemCount;
	std::map<uint64, scoped_connection> ObjectMgr::ms_itemConnections;

	void ObjectMgr::Initialize(const proto_client::Project& project)
	{
		ms_project = &project;
		ms_objectyByGuid.clear();
		ms_activePlayerGuid = 0;
		ms_itemCount.clear();
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

		if (object->GetTypeId() == ObjectTypeId::Item ||
			object->GetTypeId() == ObjectTypeId::Container)
		{
			// Initial items count for active player (TODO: This seems wrong!)
			if (ms_activePlayerGuid == 0 ||
				object->Get<uint64>(object_fields::ItemOwner) == ms_activePlayerGuid)
			{
				const uint32 itemId = object->Get<uint32>(object_fields::Entry);
				const uint32 stackCount = object->Get<uint32>(object_fields::StackCount);

				if (ms_itemCount.contains(itemId))
				{
					ms_itemCount[itemId] += stackCount;
				}
				else
				{
					ms_itemCount[itemId] = stackCount;
				}

				ms_itemConnections[itemId] = object->RegisterMirrorHandler(object_fields::StackCount, 1, &ObjectMgr::OnItemStackCountChanged);
			}
		}
	}

	void ObjectMgr::RemoveObject(const uint64 guid)
	{
		const auto it = ms_objectyByGuid.find(guid);
		if (it != ms_objectyByGuid.end())
		{
			if (it->second->GetTypeId() == ObjectTypeId::Item ||
				it->second->GetTypeId() == ObjectTypeId::Container)
			{
				if (it->second->Get<uint64>(object_fields::ItemOwner) == ms_activePlayerGuid)
				{
					const uint32 itemId = it->second->Get<uint32>(object_fields::Entry);
					const uint32 stackCount = it->second->Get<uint32>(object_fields::StackCount);

					ASSERT(ms_itemCount.contains(itemId));
					ms_itemCount[itemId] -= stackCount;
				}
			}

			// No longer watch for item field changes
			const uint32 itemId = it->second->Get<uint32>(object_fields::Entry);
			ms_itemConnections.erase(itemId);

			ms_objectyByGuid.erase(it);
		}
	}

	void ObjectMgr::RemoveAllObjects()
	{
		ms_itemCount.clear();
		ms_itemConnections.clear();
		ms_objectyByGuid.clear();
		ms_activePlayerGuid = 0;
	}

	void ObjectMgr::SetActivePlayer(uint64 guid)
	{
		ms_activePlayerGuid = guid;
	}

	uint64 ObjectMgr::GetActivePlayerGuid()
	{
		return ms_activePlayerGuid;
	}

	uint32 ObjectMgr::GetItemCount(const uint32 itemId)
	{
		if (ms_itemCount.contains(itemId))
		{
			return ms_itemCount[itemId];
		}

		return 0;
	}

	std::shared_ptr<GamePlayerC> ObjectMgr::GetActivePlayer()
	{
		if (ms_activePlayerGuid == 0)
		{
			return nullptr;
		}

		return Get<GamePlayerC>(ms_activePlayerGuid);
	}

	const proto_client::ModelDataEntry* ObjectMgr::GetModelData(const uint32 displayId)
	{
		if (!ms_project)
		{
			return nullptr;
		}

		return ms_project->models.getById(displayId);
	}

	void ObjectMgr::OnItemStackCountChanged(uint64 itemGuid)
	{
		auto item = Get<GameItemC>(itemGuid);
		ASSERT(item);

		if (item->Get<uint64>(object_fields::ItemOwner) != ms_activePlayerGuid)
		{
			return;
		}

		// Reset counter
		ms_itemCount[item->Get<uint32>(object_fields::Entry)] = 0;

		ForEachObject<GameItemC>([](const std::shared_ptr<GameItemC>& object)
			{
				if (object->Get<uint64>(object_fields::ItemOwner) != ms_activePlayerGuid)
				{
					return;
				}

				ms_itemCount[object->Get<uint32>(object_fields::Entry)] += object->Get<uint32>(object_fields::StackCount);
			});

	}
}
