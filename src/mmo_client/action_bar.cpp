
#include "action_bar.h"

namespace mmo
{
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

	void ActionBar::UseActionButton(int32 slot)
	{
		const ActionButton& button = GetActionButton(slot);
		if (button.type == action_button_type::Spell)
		{
			// TODO: Cast spell
		}
		else if (button.type == action_button_type::Item)
		{
			// TODO: Use item
		}
		else
		{
			// Unsupported action button type!
			ASSERT(false);
		}
	}

	bool ActionBar::IsValidSlot(const int32 slot)
	{
		return slot >= 0 && slot < MaxActionButtons;
	}
}
