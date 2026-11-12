// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "game/character_view.h"
#include <vector>

struct lua_State;

namespace mmo
{
	class RealmConnector;

	namespace proto_client
	{
		class Project;
	}

	/// The character selection: which character of the character list is selected, the preview that
	/// shows it, and the roster order the player arranges.
	/// @remark Lives for the whole application. Order indices used here
	///         (and by Lua) refer to the local roster order, which may differ from the realm
	///         connector's list until SaveCharacterOrder sends it.
	class CharSelect final : public NonCopyable
	{
	public:
		/// Creates the character selection.
		/// @param realmConnector Provides the character list.
		/// @param project The client project, for zone names.
		explicit CharSelect(RealmConnector& realmConnector, const proto_client::Project& project);

		~CharSelect() = default;

	public:
		/// Registers the character selection's Lua functions.
		void RegisterScriptFunctions(lua_State* lua);

		/// Registers the CharacterPreview frame type, the 3D model of the character screen.
		static void RegisterPreviewFrameFactory();

		/// Called when a character list arrived from the realm: the local order starts over from it.
		void OnCharacterListUpdated();

		/// Selects a character.
		/// @param index Order index of the character, or -1 for none.
		void SelectCharacter(int32 index);

		/// Gets the number of characters in the character list.
		int32 GetNumCharacters() const;

		/// Gets a character of the character list in roster order, or nullptr for an invalid index.
		const mmo::CharacterView* GetCharacterView(int32 index) const;

		/// Gets the order index of the selected character, or -1.
		int32 GetSelectedCharacter() const { return m_selectedCharacter; }

		/// Returns true when the character at the given index has a disabled race or class.
		[[nodiscard]] bool IsCharacterDisabled(int32 index) const;

		/// Moves a character within the local roster order. The selection stays on the same
		/// character. Nothing is sent until SaveCharacterOrder.
		/// @param fromIndex Current order index of the character.
		/// @param toIndex Order index it ends up at.
		void MoveCharacterInOrder(int32 fromIndex, int32 toIndex);

		/// Sends the local roster order to the realm, if it differs from the realm's order.
		void SaveCharacterOrder();

		/// Gets the order index to select when the character screen opens: the realm's last
		/// played character from the lastCharacters cvar, else the first one, else -1.
		[[nodiscard]] int32 GetInitialCharacterIndex() const;

		/// Remembers a character as the last played one of the connected realm (lastCharacters cvar).
		/// @param index Order index of the character.
		void SetLastCharacter(int32 index);

	private:
		/// Rebuilds the local order from the realm connector's list if it no longer holds exactly
		/// the same characters.
		void EnsureOrder() const;

		/// Gets the realm connector's character with the given guid, or nullptr.
		const CharacterView* FindCharacter(uint64 guid) const;

		/// Gets the name of a character's zone, or an empty string.
		String GetCharacterZoneName(int32 index) const;

	private:
		RealmConnector& m_realmConnector;
		const proto_client::Project& m_project;

		int32 m_selectedCharacter = -1;

		/// Character guids in local roster order. Rebuilt lazily from the realm connector.
		mutable std::vector<uint64> m_order;
	};
}
