#pragma once

#include "base/typedefs.h"

namespace mmo
{
	enum class CursorItemType
	{
		None,

		Item,

		Spell,

		ActionButton,
	};

	enum class CursorType
	{
		Pointer,

		Interact,

		Attack,

		Loot,

		Gossip,
	};

	class Cursor
	{
	public:
		void SetCursorType(CursorType type);

		CursorType GetCursorType() const { return m_cursorType; }

		bool LoadCursorTypeFromTexture(CursorType type, const String& textureFileName);

		void Clear();

		void SetItem(uint32 slot);

		void SetSpell(uint32 spell);

		void SetActionButton(uint32 slot);

		uint32 GetCursorItem() const;

		/// Gets the cursor item type.
		CursorItemType GetItemType() const { return m_type; }

	private:
		CursorType m_cursorType = CursorType::Pointer;

		CursorItemType m_type = CursorItemType::None;

		uint32 m_itemSlot = static_cast<uint32>(-1);
	};
}
