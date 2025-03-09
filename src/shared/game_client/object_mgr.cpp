
#include "object_mgr.h"

#include "game_item_c.h"
#include "unit_handle.h"
#include "base/macros.h"
#include "client_data/project.h"
#include "mmo_client/party_info.h"
#include "mmo_client/party_unit_handle.h"

namespace mmo
{
	MovementGlobals ObjectMgr::ms_movementGlobals;
	std::map<uint64, std::shared_ptr<GameObjectC>> ObjectMgr::ms_objectyByGuid;
	uint64 ObjectMgr::ms_activePlayerGuid = 0;
	uint64 ObjectMgr::ms_selectedObjectGuid = 0;
	const proto_client::Project* ObjectMgr::ms_project = nullptr;

	std::map<uint32, uint32> ObjectMgr::ms_itemCount;
	std::map<uint64, scoped_connection> ObjectMgr::ms_itemConnections;
	PartyInfo* ObjectMgr::ms_partyInfo = nullptr;

	void ObjectMgr::Initialize(const proto_client::Project& project, PartyInfo& partyInfo)
	{
		ms_project = &project;
		ms_objectyByGuid.clear();
		ms_activePlayerGuid = 0;
		ms_selectedObjectGuid = 0;
		ms_itemCount.clear();
		ms_partyInfo = &partyInfo;
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

	std::shared_ptr<UnitHandle> ObjectMgr::GetUnitHandleByName(const std::string& unitName)
	{
		if (unitName == "player")
		{
			if (const auto player = ObjectMgr::GetActivePlayer())
			{
				return std::make_shared<UnitHandle>(*player);
			}
		}
		else if (unitName == "target")
		{
			if (const auto playerObject = ObjectMgr::GetActivePlayer())
			{
				const uint64 targetGuid = playerObject->Get<uint64>(object_fields::TargetUnit);
				if (const auto target = ObjectMgr::Get<GameUnitC>(targetGuid); target)
				{
					return std::make_shared<UnitHandle>(*target);
				}

				if (int32 index = ms_partyInfo->GetMemberIndexByGuid(targetGuid); index >= 0)
				{
					return std::make_shared<PartyUnitHandle>(*ms_partyInfo, index);
				}
			}
		}
		else if (unitName.starts_with("party"))
		{
			if (!ms_partyInfo)
			{
				return nullptr;
			}

			// Read party member index from string and parse it to integer
			const int32 partyIndex = std::stoi(unitName.substr(5));
			if (partyIndex <= 0 || partyIndex > 4)
			{
				ELOG("Wrong party index, allowed unit is party1-4!");
				return nullptr;
			}

			const uint64 memberGuid = ms_partyInfo->GetMemberGuid(partyIndex - 1);
			if (memberGuid == 0)
			{
				return nullptr;
			}

			if (const auto partyMember = ObjectMgr::Get<GamePlayerC>(memberGuid); partyMember)
			{
				return std::make_shared<PartyUnitHandle>(*ms_partyInfo, *partyMember, partyIndex - 1);
			}

			return std::make_shared<PartyUnitHandle>(*ms_partyInfo, partyIndex - 1);
		}

		return nullptr;
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

	uint64 ObjectMgr::GetSelectedObjectGuid()
	{
		return ms_selectedObjectGuid;
	}

	void ObjectMgr::SetSelectedObjectGuid(uint64 guid)
	{
		ms_selectedObjectGuid = guid;
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
