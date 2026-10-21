// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "game/character_view.h"

struct lua_State;

namespace mmo
{
	class GlueScene;
	class RealmConnector;

	/// The character selection: which character of the character list is selected, shown on the
	/// Haven plaza of the glue scene.
	/// @remark Lives for the whole application, longer than any glue scene.
	class CharSelect final : public NonCopyable
	{
	public:
		/// Creates the character selection.
		/// @param realmConnector Provides the character list.
		explicit CharSelect(RealmConnector& realmConnector);

		~CharSelect() = default;

	public:
		/// Registers the character selection's Lua functions.
		void RegisterScriptFunctions(lua_State* lua);

		/// Sets the glue scene whose plaza shows the characters. Called with the scene when the
		/// login state is entered and with nullptr before the scene is destroyed.
		/// @param scene The glue scene, or nullptr.
		void SetGlueScene(GlueScene* scene);

		/// Selects a character and places the characters on the plaza for it. The first valid
		/// selection after the glue scene was set (or after the roster became empty) places everyone
		/// instantly; later ones make them walk. An invalid index (-1) only records the selection
		/// and leaves the plaza untouched, unless the roster is empty, in which case the plaza is
		/// cleared.
		/// @param index Order index of the character, or -1 for none.
		void SelectCharacter(int32 index);

		/// Gets the number of characters in the character list.
		int32 GetNumCharacters() const;

		/// Gets a character of the character list, or nullptr for an invalid index.
		const mmo::CharacterView* GetCharacterView(int32 index) const;

		/// Gets the order index of the selected character, or -1.
		int32 GetSelectedCharacter() const { return m_selectedCharacter; }

		/// Returns true when the character at the given index has a disabled race or class.
		[[nodiscard]] bool IsCharacterDisabled(int32 index) const;

	private:
		RealmConnector& m_realmConnector;

		/// Not owned. Null while no login state is active.
		GlueScene* m_glueScene = nullptr;

		/// Whether a valid selection was placed on the plaza since the glue scene was set, so
		/// further selections animate.
		bool m_plazaPlaced = false;

		int32 m_selectedCharacter = -1;
	};
}
