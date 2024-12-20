
#include "action_bar.h"

#include "cursor.h"
#include "frame_ui/frame_mgr.h"
#include "game/spell_target_map.h"

namespace mmo
{
	extern Cursor g_cursor;

	ActionBar::ActionBar(RealmConnector& realmConnector, const proto_client::SpellManager& spells, DBItemCache& items)
		: m_connector(realmConnector)
		, m_spells(spells)
		, m_items(items)
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

		return GetActionButton(slot).type != action_button_type::None;
	}

	bool ActionBar::IsActionButtonSpell(int32 slot) const
	{
		if (!IsValidSlot(slot))
		{
			return false;
		}

		return GetActionButton(slot).type == action_button_type::Spell;
	}

	bool ActionBar::IsActionButtonItem(int32 slot) const
	{
		if (!IsValidSlot(slot))
		{
			return false;
		}

		return GetActionButton(slot).type == action_button_type::Item;
	}

	const proto_client::SpellEntry* ActionBar::GetActionButtonSpell(int32 slot) const
	{
		if (!IsActionButtonSpell(slot))
		{
			return nullptr;
		}

		return m_spells.getById(GetActionButton(slot).action);
	}

	const ItemInfo* ActionBar::GetActionButtonItem(int32 slot) const
	{
		// TODO
		return nullptr;
	}

	void ActionBar::UseActionButton(int32 slot) const
	{
		const ActionButton& button = GetActionButton(slot);
		if (button.type == action_button_type::Spell)
		{
			// TODO: Target map!
			SpellTargetMap targetMap;
			m_connector.CastSpell(button.action, targetMap);
		}
		else if (button.type == action_button_type::Item)
		{
			// TODO: Use item
		}
		else
		{
			WLOG("Action button is empty, nothing to do!");
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
			if (m_actionButtons[slot].type != action_button_type::None)
			{
				ELOG("Cannot pickup action button " << slot << " because it is empty!");
				return;
			}

			g_cursor.SetActionButton(slot);
			return;
		}

		// We do have an item, place it at the action button slot
		switch (g_cursor.GetItemType())
		{
		case CursorItemType::Item:
			m_actionButtons[slot].type = action_button_type::Item;
			// TODO: Get item from inventory index
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
