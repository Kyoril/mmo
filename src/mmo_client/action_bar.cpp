
#include "action_bar.h"

#include "cursor.h"
#include "spell_cast.h"
#include "frame_ui/frame_mgr.h"
#include "game/spell_target_map.h"
#include "game_client/object_mgr.h"

namespace mmo
{
	extern Cursor g_cursor;

	ActionBar::ActionBar(RealmConnector& realmConnector, const proto_client::SpellManager& spells, DBItemCache& items, SpellCast& spellCast)
		: m_connector(realmConnector)
		, m_spells(spells)
		, m_items(items)
		, m_spellCast(spellCast)
	{
	}

	const ActionButton& ActionBar::GetActionButton(const int32 slot) const
	{
		ASSERT(IsValidSlot(slot));
		return m_actionButtons[slot];
	}

	bool ActionBar::IsActionButtonUsable(const int32 slot) const
	{
		if (!IsValidSlot(slot))
		{
			return false;
		}

		const ActionButton& actionButton = GetActionButton(slot);
		if (actionButton.type == action_button_type::None)
		{
			// Hacky way to allow empty action buttons
			return true;
		}

		// Action button is usable if it has a valid action
		if (actionButton.action == 0)
		{
			return true;
		}

		switch (actionButton.type)
		{
		case action_button_type::Item:
			{
			auto* entry = m_items.Get(actionButton.action);
			if (!entry)
			{
				return false;
			}

			if (ObjectMgr::GetItemCount(actionButton.action) == 0)
			{
				return false;
			}

			bool isUsable = false;
			for (const ItemSpell& spell : entry->spells)
			{
				if (spell.triggertype == item_spell_trigger::OnUse)
				{
					isUsable = true;
					break;
				}
			}

			return isUsable;
			}
			
		case action_button_type::Spell:
		{
			const auto* spell = m_spells.getById(actionButton.action);
			if (!spell)
			{
				return false;
			}

			auto player = ObjectMgr::GetActivePlayer();
			if (!player)
			{
				return false;
			}

			if (!player->HasSpell(spell->id()))
			{
				return false;
			}

			if (spell->powertype() != player->GetPowerType())
			{
				return false;
			}

			if (spell->cost() > 0 && player->GetPower(spell->powertype()) < spell->cost())
			{
				return false;
			}

			return true;
		}
		}

		return false;
	}

	bool ActionBar::IsActionButtonSpell(const int32 slot) const
	{
		if (!IsValidSlot(slot))
		{
			return false;
		}

		return GetActionButton(slot).type == action_button_type::Spell;
	}

	bool ActionBar::IsActionButtonItem(const int32 slot) const
	{
		if (!IsValidSlot(slot))
		{
			return false;
		}

		return GetActionButton(slot).type == action_button_type::Item;
	}

	const proto_client::SpellEntry* ActionBar::GetActionButtonSpell(const int32 slot) const
	{
		if (!IsActionButtonSpell(slot))
		{
			return nullptr;
		}

		return m_spells.getById(GetActionButton(slot).action);
	}

	const ItemInfo* ActionBar::GetActionButtonItem(const int32 slot) const
	{
		if (!IsActionButtonItem(slot))
		{
			return nullptr;
		}

		return m_items.Get(GetActionButton(slot).action);
	}

	void ActionBar::UseActionButton(const int32 slot)
	{
		if (g_cursor.GetItemType() == CursorItemType::None)
		{
			const ActionButton& button = GetActionButton(slot);
			if (button.type == action_button_type::Spell)
			{
				// Start casting a spell
				m_spellCast.CastSpell(button.action);
			}
			else if (button.type == action_button_type::Item)
			{
				uint8 bag, slot;
				uint64 guid;
				if (!ObjectMgr::FindItem(button.action, bag, slot, guid))
				{
					return;
				}

				SpellTargetMap targetMap;
				m_connector.UseItem(bag, slot, guid, targetMap);
			}
		}
		else
		{
			// Pickup action button
			PickupActionButton(slot);
		}
	}

	void ActionBar::PickupActionButton(const int32 slot)
	{
		if (!IsValidSlot(slot))
		{
			ELOG("Invalid action button slot " << slot);
			return;
		}

		// Do we already have an item?
		if (g_cursor.GetItemType() == CursorItemType::None)
		{
			// No, so pick up the action button if there is one
			if (m_actionButtons[slot].type == action_button_type::None)
			{
				return;
			}

			g_cursor.SetActionButton(slot);
			return;
		}

		const std::shared_ptr<GamePlayerC> player = ObjectMgr::GetActivePlayer();
		ASSERT(player);

		// We do have an item, place it at the action button slot
		switch (g_cursor.GetItemType())
		{
		case CursorItemType::Item:
			{
				const uint8 bag = static_cast<uint8>(g_cursor.GetCursorItem() >> 8) & 0xFF;
				const uint8 bagSlot = g_cursor.GetCursorItem() & 0xFF;

				uint64 itemGuid = 0;

				if (bag == player_inventory_slots::Bag_0)
				{
					itemGuid = player->Get<uint64>(object_fields::InvSlotHead + bagSlot * 2);
				}
				else
				{
					if (const uint64 bagGuid = player->Get<uint64>(object_fields::InvSlotHead + bag * 2); bagGuid != 0)
					{
						if (const std::shared_ptr<GameBagC> bag = ObjectMgr::Get<GameBagC>(bagGuid); bag && bagSlot < bag->GetBagSlots())
						{
							itemGuid = bag->Get<uint64>(object_fields::Slot_1 + bagSlot * 2);
						}
					}
				}

				if (itemGuid != 0)
				{
					const std::shared_ptr<GameItemC> item = ObjectMgr::Get<GameItemC>(itemGuid);
					if (item && item->GetEntry())
					{
						m_actionButtons[slot].type = action_button_type::Item;
						m_actionButtons[slot].action = static_cast<uint16>(item->GetEntry()->id);
						ActionButtonChanged(slot);
					}
				}
			}
			break;
		case CursorItemType::Spell:
			// Assign spell
			m_actionButtons[slot].type = action_button_type::Spell;
			m_actionButtons[slot].action = static_cast<uint16>(g_cursor.GetCursorItem());
			ActionButtonChanged(slot);
			break;
		case CursorItemType::ActionButton:
			// Flip action buttons if they are not the same
			if (g_cursor.GetCursorItem() != slot)
			{
				std::swap(m_actionButtons[slot], m_actionButtons[g_cursor.GetCursorItem()]);
				ActionButtonChanged(slot);
				ActionButtonChanged(g_cursor.GetCursorItem());
			}
			break;
		}

		// Clear the cursor item
		g_cursor.Clear();

		// Raise UI event
		FrameManager::Get().TriggerLuaEvent("ACTION_BAR_CHANGED");
	}

	void ActionBar::OnActionButtons(io::Reader& reader)
	{
		if (!(reader >> io::read_range(m_actionButtons)))
		{
			return;
		}

		FrameManager::Get().TriggerLuaEvent("ACTION_BAR_CHANGED");
	}

	void ActionBar::SetActionButton(int32 slot, const ActionButton& button)
	{
		ASSERT(IsValidSlot(slot));

		m_actionButtons[slot] = button;
		ActionButtonChanged(slot);
	}

	void ActionBar::ClearActionButton(const int32 slot)
	{
		ASSERT(IsValidSlot(slot));

		m_actionButtons[slot] = {};
		ActionButtonChanged(slot);
	}

	bool ActionBar::IsValidSlot(const int32 slot)
	{
		return slot >= 0 && slot < MaxActionButtons;
	}

	void ActionBar::ActionButtonChanged(const int32 slot) const
	{
		ASSERT(IsValidSlot(slot));
		m_connector.SetActionBarButton(slot, m_actionButtons[slot]);
	}
}
