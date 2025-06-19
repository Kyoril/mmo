#pragma once

#include "base/typedefs.h"
#include <memory>

namespace mmo
{
	namespace proto_client
	{
		class Project;
		class SpellEntry;
	}

	class GameItemC;

	enum class CursorItemType
	{
		None,

		Item,

		Spell
	};

	enum class CursorType
	{
		Pointer,

		Interact,

		Attack,

		Loot,

		Gossip,
	};

	/// Manages the game cursor state and appearance, including dragged items and spells.
	/// The cursor can display different types of icons based on the currently dragged
	/// item or spell, dynamically resolving the appropriate texture from the game data.
	class Cursor
	{
	public:
		/// Initializes the cursor system with access to game data for icon resolution.
		/// @param project Reference to the project data containing spell and item information.
		void Initialize(const proto_client::Project& project);

	public:
		/// Sets the cursor type (pointer, interact, attack, etc.).
		/// @param type The cursor type to set.
		void SetCursorType(CursorType type);

		/// Gets the current cursor type.
		/// @return The current cursor type.
		CursorType GetCursorType() const { return m_cursorType; }

		/// Loads a cursor texture for a specific cursor type.
		/// @param type The cursor type to associate with the texture.
		/// @param textureFileName The texture file to load.
		/// @return True if successful, false otherwise.
		bool LoadCursorTypeFromTexture(CursorType type, const String& textureFileName);

		/// Clears the current cursor item/spell and resets the cursor icon.
		void Clear();

		/// Sets an item to be displayed on the cursor by resolving the slot to the actual item.
		/// The cursor will display the item's icon, or a default icon if resolution fails.
		/// @param slot The inventory slot containing the item to display.
		void SetItem(uint32 slot);

		/// Sets a spell to be displayed on the cursor by resolving the spell ID.
		/// The cursor will display the spell's icon, or a default icon if resolution fails.
		/// @param spell The spell ID to display.
		void SetSpell(uint32 spell);

		/// Gets the currently selected cursor item slot or spell ID.
		/// @return The item slot or spell ID, or -1 if none is set.
		uint32 GetCursorItem() const;

		/// Gets the cursor item type (none, item, or spell).
		/// @return The current cursor item type.
		CursorItemType GetItemType() const { return m_type; }

	private:
		/// Resolves an item slot to the actual GameItemC instance.
		/// @param slot The inventory slot to resolve.
		/// @return Shared pointer to the item, or nullptr if not found.
		std::shared_ptr<GameItemC> ResolveItemFromSlot(uint32 slot) const;

		/// Updates the cursor icon based on the current item or spell.
		void UpdateCursorIcon();

		/// Gets the default fallback icon path.
		/// @return The default icon texture path.
		const char* GetDefaultIcon() const;
	private:
		CursorType m_cursorType = CursorType::Pointer;

		CursorItemType m_type = CursorItemType::None;

		uint32 m_itemSlot = static_cast<uint32>(-1);

		/// Reference to the project data for spell and item resolution.
		const proto_client::Project* m_project = nullptr;
	};
}
