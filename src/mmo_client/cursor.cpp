
#include "cursor.h"

namespace mmo
{
	void Cursor::Clear()
	{
		m_type = CursorItemType::None;
		m_itemSlot = -1;
	}

	void Cursor::SetItem(uint32 slot)
	{
		m_type = CursorItemType::Item;
		m_itemSlot = slot;
	}

	uint32 Cursor::GetCursorItem() const
	{
		return m_itemSlot;
	}
}
