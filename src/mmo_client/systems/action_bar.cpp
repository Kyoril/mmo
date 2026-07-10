
#include "action_bar.h"

#include "cursor.h"
#include "spell_cast.h"
#include "frame_ui/frame_mgr.h"
#include "game/spell.h"
#include "game/spell_target_map.h"
#include "game_client/game_bag_c.h"
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

			// A "Disabled While Active" spell whose aura is currently active stays usable regardless
			// of power/cost, because re-activating it only cancels the aura (which costs nothing).
			if ((spell->attributes(0) & spell_attributes::DisabledWhileActive) != 0 && player->HasAura(spell->id()))
			{
				return true;
			}

			if (spell->powertype() != player->GetPowerType())
			{
				return false;
			}

			if (spell->cost() > 0 && player->GetPower(spell->powertype()) < static_cast<int32>(player->ApplySpellModForFlags(static_cast<uint8>(spell_mod_op::Cost), spell->cost(), spell->familyflags())))
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

	bool ActionBar::IsActionButtonActive(const int32 slot) const
	{
		const auto* spell = GetActionButtonSpell(slot);
		if (!spell)
		{
			return false;
		}

		// Only "Disabled While Active" spells can be in the active/toggled state.
		if ((spell->attributes(0) & spell_attributes::DisabledWhileActive) == 0)
		{
			return false;
		}

		const auto player = ObjectMgr::GetActivePlayer();
		if (!player)
		{
			return false;
		}

		return player->HasAura(spell->id());
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

		const std::shared_ptr<GamePlayerC> player = ObjectMgr::GetActivePlayer();
		ASSERT(player);

		// Do we have something picked up already?
		if (g_cursor.GetItemType() == CursorItemType::None)
		{
			// No, so pick up the action button if there is one
			if (m_actionButtons[slot].type == action_button_type::None)
			{
				return;
			}

			switch (m_actionButtons[slot].type)
			{
			case action_button_type::Spell:
				g_cursor.SetSpell(m_actionButtons[slot].action);
				break;
			case action_button_type::Item:
				uint8 bag, bagSlot;
				uint64 guid;
				if (!ObjectMgr::FindItem(m_actionButtons[slot].action, bag, bagSlot, guid))
				{
					return;
				}

				g_cursor.SetItem((static_cast<uint16>(bag) << 8) | bagSlot);
				break;
			}

			ClearActionButton(slot);
			FrameManager::Get().TriggerLuaEvent("ACTION_BAR_CHANGED");
			return;
		}

		// We do have an item, place it at the action button slot. Remember what was in the
		// slot before, so an occupied slot swaps its action onto the cursor instead of losing it.
		const ActionButton previous = m_actionButtons[slot];
		bool placed = false;

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
						EnsureItemData(m_actionButtons[slot].action);
						ActionButtonChanged(slot);
						placed = true;
					}
				}
			}
			break;
		case CursorItemType::Spell:
			{
				const uint32 spellId = g_cursor.GetCursorItem();

				// Passive abilities can never be triggered manually, so they must not be placed
				// on the action bar. Reject the placement and inform the player.
				const auto* spell = m_spells.getById(spellId);
				if (spell != nullptr && (spell->attributes(0) & spell_attributes::Passive) != 0)
				{
					FrameManager::Get().TriggerLuaEvent("UI_ERROR_MESSAGE", "ACTION_BAR_PASSIVE_SPELL_ERROR");
					g_cursor.Clear();
					return;
				}

				// Assign spell
				m_actionButtons[slot].type = action_button_type::Spell;
				m_actionButtons[slot].action = static_cast<uint16>(spellId);
				ActionButtonChanged(slot);
				placed = true;
			}
			break;
		}

		// If the slot was occupied by a different action, pick that action up so the player can
		// keep rearranging. Dropping the same action back onto its slot just clears the cursor.
		bool pickedUpPrevious = false;
		if (placed && previous.type != action_button_type::None &&
			(previous.type != m_actionButtons[slot].type || previous.action != m_actionButtons[slot].action))
		{
			switch (previous.type)
			{
			case action_button_type::Spell:
				g_cursor.SetSpell(previous.action);
				pickedUpPrevious = true;
				break;
			case action_button_type::Item:
				uint8 bag, bagSlot;
				uint64 guid;
				if (ObjectMgr::FindItem(previous.action, bag, bagSlot, guid))
				{
					g_cursor.SetItem((static_cast<uint16>(bag) << 8) | bagSlot);
					pickedUpPrevious = true;
				}
				break;
			}
		}

		if (!pickedUpPrevious)
		{
			// Clear the cursor item
			g_cursor.Clear();
		}

		// Raise UI event
		FrameManager::Get().TriggerLuaEvent("ACTION_BAR_CHANGED");
	}

	void ActionBar::OnActionButtons(io::Reader& reader)
	{
		if (!(reader >> io::read_range(m_actionButtons)))
		{
			return;
		}

		// Action buttons loaded from the realm may reference items whose data isn't cached yet
		// (e.g. on fresh login). Request the missing item data so the icons appear once it arrives.
		EnsureItemDataForAllButtons();

		FrameManager::Get().TriggerLuaEvent("ACTION_BAR_CHANGED");
	}

	void ActionBar::SetActionButton(int32 slot, const ActionButton& button)
	{
		ASSERT(IsValidSlot(slot));

		m_actionButtons[slot] = button;

		if (button.type == action_button_type::Item)
		{
			EnsureItemData(button.action);
		}

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

	void ActionBar::EnsureItemData(const uint16 itemId)
	{
		if (itemId == 0)
		{
			return;
		}

		// Already cached, nothing to do.
		if (m_items.IsCached(itemId))
		{
			return;
		}

		// Avoid registering a duplicate callback for an item we already requested.
		if (!m_pendingItemRequests.insert(itemId).second)
		{
			return;
		}

		// Request the item data and refresh the action bar once the async response arrives.
		m_items.Get(itemId, [this, itemId](uint64, const ItemInfo&)
		{
			m_pendingItemRequests.erase(itemId);
			FrameManager::Get().TriggerLuaEvent("ACTION_BAR_CHANGED");
		});
	}

	void ActionBar::EnsureItemDataForAllButtons()
	{
		for (const ActionButton& button : m_actionButtons)
		{
			if (button.type == action_button_type::Item)
			{
				EnsureItemData(button.action);
			}
		}
	}
}
