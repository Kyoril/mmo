// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "char_select.h"

#include "glue/glue_scene.h"
#include "glue/plaza_avatars.h"
#include "net/realm_connector.h"

#include "luabind_lambda.h"

namespace mmo
{
	CharSelect::CharSelect(RealmConnector& realmConnector)
		: m_realmConnector(realmConnector)
	{
	}

	void CharSelect::RegisterScriptFunctions(lua_State* lua)
	{
		LUABIND_MODULE(lua,
			luabind::def_lambda("GetNumCharacters", [this]() { return GetNumCharacters(); }),
			luabind::def_lambda("GetCharacterInfo", [this](int32 index) { return GetCharacterView(index); }),
			luabind::def_lambda("SelectCharacter", [this](int32 index) { return SelectCharacter(index); }),
			luabind::def_lambda("IsCharacterDisabled", [this](int32 index) { return IsCharacterDisabled(index); })
		);
	}

	void CharSelect::SetGlueScene(GlueScene* scene)
	{
		// The plaza and every avatar on it belong to the scene: nothing of it is kept here, so
		// dropping the pointer is all that is needed when the scene goes away.
		m_glueScene = scene;
		m_plazaPlaced = false;
	}

	void CharSelect::SelectCharacter(const int32 index)
	{
		m_selectedCharacter = index;

		if (!m_glueScene)
		{
			return;
		}

		PlazaAvatars& plaza = m_glueScene->GetPlazaAvatars();

		// Cheap when the roster did not change: avatars are only rebuilt for new characters.
		plaza.SetCharacters(m_realmConnector.GetCharacterViews());

		if (index >= 0 && index < GetNumCharacters())
		{
			plaza.Select(index, m_plazaPlaced);
			m_plazaPlaced = true;
			return;
		}

		if (GetNumCharacters() == 0)
		{
			// Nobody left to show (e.g. every character was deleted).
			plaza.Select(-1, false);
			m_plazaPlaced = false;
		}

		// Otherwise "no selection" is a transient state of the character screen (it clears the
		// selection before picking one): leave the plaza as it is, so the front character neither
		// walks back to its spot nor restarts its pose, and the first real selection stays instant.
	}

	int32 CharSelect::GetNumCharacters() const
	{
		return static_cast<int32>(m_realmConnector.GetCharacterViews().size());
	}

	const mmo::CharacterView* CharSelect::GetCharacterView(const int32 index) const
	{
		if (index < 0 || index >= GetNumCharacters())
		{
			return nullptr;
		}

		return &m_realmConnector.GetCharacterViews()[index];
	}

	bool CharSelect::IsCharacterDisabled(int32 index) const
	{
		if (index < 0 || index >= GetNumCharacters())
		{
			return false;
		}

		const auto& view = m_realmConnector.GetCharacterViews()[index];
		return m_realmConnector.IsRaceDisabled(view.GetRaceId()) ||
			   m_realmConnector.IsClassDisabled(view.GetClassId());
	}
}
