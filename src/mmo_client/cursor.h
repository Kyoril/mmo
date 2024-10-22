#pragma once

#include "base/typedefs.h"

namespace mmo
{
	enum class CursorItemType
	{
		None,

		Item,
		Spell,
	};

	class Cursor
	{
	public:


		void Clear();

		void SetItem(uint32 slot);

		uint32 GetCursorItem() const;

		/// Gets the cursor item type.
		CursorItemType GetType() const { return m_type; }

	private:
		CursorItemType m_type = CursorItemType::None;

		uint32 m_itemSlot = 0;
	};
}
