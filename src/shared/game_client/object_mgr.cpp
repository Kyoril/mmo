
#include "object_mgr.h"

#include "game_item_c.h"
#include "unit_handle.h"
#include "base/macros.h"
#include "client_data/project.h"
#include "mmo_client/party_unit_handle.h"
#include "mmo_client/systems/party_info.h"

namespace mmo
{
	MovementGlobals ObjectMgr::ms_movementGlobals;
	std::map<uint64, std::shared_ptr<GameObjectC>> ObjectMgr::ms_objectyByGuid;
	uint64 ObjectMgr::ms_activePlayerGuid = 0;
	uint64 ObjectMgr::ms_selectedObjectGuid = 0;
	uint64 ObjectMgr::ms_hoveredObjectGuid = 0;
	const proto_client::Project* ObjectMgr::ms_project = nullptr;

	std::map<uint32, uint32> ObjectMgr::ms_itemCount;
	std::map<uint64, scoped_connection> ObjectMgr::ms_itemConnections;
	PartyInfo* ObjectMgr::ms_partyInfo = nullptr;

	FontPtr ObjectMgr::ms_unitNameFont = nullptr;
	MaterialPtr ObjectMgr::ms_unitNameFontMaterial = nullptr;

	void ObjectMgr::Initialize(const proto_client::Project& project, PartyInfo& partyInfo)
	{
		ms_project = &project;
		ms_objectyByGuid.clear();
		ms_activePlayerGuid = 0;
		ms_selectedObjectGuid = 0;
		ms_hoveredObjectGuid = 0;
		ms_itemCount.clear();
		ms_partyInfo = &partyInfo;
	}

	void ObjectMgr::SetUnitNameFontSettings(FontPtr font, MaterialPtr material)
	{
		ms_unitNameFont = font;
		ms_unitNameFontMaterial = material;

		// TODO: Refresh all unit names?
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

				ms_itemConnections[object->GetGuid()] = object->RegisterMirrorHandler(object_fields::StackCount, 1, &ObjectMgr::OnItemStackCountChanged);
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

					// Remove the connection for this specific item instance
					ms_itemConnections.erase(guid);

					// If this was the last item of this type, remove the item count entry
					if (ms_itemCount[itemId] == 0)
					{
						ms_itemCount.erase(itemId);
					}
				}
			}

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
			const auto selectedObject = ObjectMgr::GetSelectedObject();
			if (selectedObject)
			{
				return std::make_shared<UnitHandle>(*selectedObject);
			}

			if (int32 index = ms_partyInfo->GetMemberIndexByGuid(ObjectMgr::GetSelectedObjectGuid()); index >= 0)
			{
				return std::make_shared<PartyUnitHandle>(*ms_partyInfo, index);
			}
		}
		else if (unitName == "mouseover")
		{
			const auto hoveredUnit = ObjectMgr::GetHoveredUnit();
			if (hoveredUnit)
			{
				return std::make_shared<UnitHandle>(*hoveredUnit);
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

	bool ObjectMgr::FindItem(uint32 entryId, uint8& out_bag, uint8& out_slot, uint64& out_guid)
	{
		const auto player = GetActivePlayer();
		if (!player)
		{
			return false;
		}

		// Check player inventory
		for (uint8 slot = player_inventory_pack_slots::Start; slot < player_inventory_pack_slots::End; ++slot)
		{
			const uint64 guid = player->Get<uint64>(object_fields::PackSlot_1 + (slot - player_inventory_pack_slots::Start) * 2);
			if (!guid)
			{
				continue;
			}

			if (const auto item = Get<GameItemC>(guid); item && item->Get<uint32>(object_fields::Entry) == entryId)
			{
				out_bag = player_inventory_slots::Bag_0;
				out_slot = slot;
				out_guid = guid;
				return true;
			}
		}

		// Check player bags
		for (uint8 bag = player_inventory_slots::Start; bag < player_inventory_slots::End; ++bag)
		{
			const uint64 bagGuid = player->Get<uint64>(object_fields::InvSlotHead + bag * 2);
			if (const auto bagItem = Get<GameBagC>(bagGuid); bagItem)
			{
				for (uint8 slot = 0; slot < bagItem->Get<uint32>(object_fields::NumSlots); ++slot)
				{
					const uint64 itemGuid = bagItem->Get<uint64>(object_fields::Slot_1 + slot * 2);
					if (itemGuid == 0)
					{
						continue;
					}

					if (const auto item = Get<GameItemC>(itemGuid); item && item->Get<uint32>(object_fields::Entry) == entryId)
					{
						out_bag = bag;
						out_slot = slot;
						out_guid = itemGuid;
						return true;
					}
				}
			}
		}

		return false;
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

	std::shared_ptr<GameUnitC> ObjectMgr::GetSelectedObject()
	{
		if (ms_selectedObjectGuid == 0)
		{
			return nullptr;
		}

		return Get<GameUnitC>(ms_selectedObjectGuid);
	}

	uint64 ObjectMgr::GetHoveredObjectGuid()
	{
		return ms_hoveredObjectGuid;
	}

	std::shared_ptr<GameUnitC> ObjectMgr::GetHoveredUnit()
	{
		if (ms_hoveredObjectGuid == 0)
		{
			return nullptr;
		}
		return Get<GameUnitC>(ms_hoveredObjectGuid);
	}

	void ObjectMgr::SetHoveredObject(uint64 hoveredObjectGuid)
	{
		ms_hoveredObjectGuid = hoveredObjectGuid;
	}

	void ObjectMgr::SetSelectedObjectGuid(uint64 guid)
	{
		if (GetSelectedObject())
		{
			GetSelectedObject()->SetUnitNameVisible(false);
		}

		ms_selectedObjectGuid = guid;

		if (GetSelectedObject())
		{
			GetSelectedObject()->SetUnitNameVisible(true);
		}
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

	void ObjectMgr::OnItemStackCountChanged(const uint64 itemGuid)
	{
		const auto item = Get<GameItemC>(itemGuid);
		ASSERT(item);

		if (item->Get<uint64>(object_fields::ItemOwner) != ms_activePlayerGuid)
		{
			return;
		}

		// Reset counter
		const uint32 affectedEntry = item->Get<uint32>(object_fields::Entry);
		ms_itemCount[item->Get<uint32>(object_fields::Entry)] = 0;

		ForEachObject<GameItemC>([affectedEntry](const std::shared_ptr<GameItemC>& object)
			{
				if (object->Get<uint64>(object_fields::ItemOwner) != ms_activePlayerGuid)
				{
					return;
				}

				// This is a different entry, skip it
				if (object->Get<uint32>(object_fields::Entry) != affectedEntry)
				{
					return;
				}

				ms_itemCount[object->Get<uint32>(object_fields::Entry)] += object->Get<uint32>(object_fields::StackCount);
			});
	}
}
