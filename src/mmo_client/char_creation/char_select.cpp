// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "char_select.h"

#include "console/console_var.h"
#include "glue/glue_scene.h"
#include "glue/plaza_avatars.h"
#include "net/realm_connector.h"

#include "client_data/project.h"
#include "frame_ui/frame_mgr.h"
#include "game_client/plaza_roster.h"

#include "luabind_lambda.h"

#include <algorithm>

namespace luabind
{
	// Combines several policies (same helper as in game_script.cpp).
	template <typename... T>
	using joined = typename luabind::meta::join<T...>::type;
}

namespace mmo
{
	namespace
	{
		/// Name of the cvar remembering the last played character per realm.
		const char* const lastCharactersVarName = "lastCharacters";

		/// Seconds the camera takes to push in on the front character before entering the world.
		constexpr float pushInSeconds = 0.8f;

		/// Height above a plaza spot where its "new character" marker hangs, in metres.
		constexpr float spotMarkerHeight = 1.0f;
	}

	CharSelect::CharSelect(RealmConnector& realmConnector, const proto_client::Project& project)
		: m_realmConnector(realmConnector)
		, m_project(project)
	{
	}

	void CharSelect::RegisterScriptFunctions(lua_State* lua)
	{
		LUABIND_MODULE(lua,
			luabind::def_lambda("GetNumCharacters", [this]() { return GetNumCharacters(); }),
			luabind::def_lambda("GetCharacterInfo", [this](int32 index) { return GetCharacterView(index); }),
			luabind::def_lambda("SelectCharacter", [this](int32 index) { return SelectCharacter(index); }),
			luabind::def_lambda("GetSelectedCharacterIndex", [this]() { return GetSelectedCharacter(); }),
			luabind::def_lambda("IsCharacterDisabled", [this](int32 index) { return IsCharacterDisabled(index); }),
			luabind::def_lambda("GetCharacterZoneName", [this](int32 index) { return GetCharacterZoneName(index); }),
			luabind::def_lambda("GetInitialCharacterIndex", [this]() { return GetInitialCharacterIndex(); }),
			luabind::def_lambda("SetLastCharacter", [this](int32 index) { SetLastCharacter(index); }),
			luabind::def_lambda("MoveCharacterInOrder", [this](int32 fromIndex, int32 toIndex) { MoveCharacterInOrder(fromIndex, toIndex); }),
			luabind::def_lambda("SaveCharacterOrder", [this]() { SaveCharacterOrder(); }),

			luabind::def_lambda("RotateCharacter", [this](float degrees)
				{
					if (m_glueScene)
					{
						m_glueScene->GetPlazaAvatars().Rotate(degrees);
					}
				}),

			luabind::def_lambda("PlazaCharacterLeave", [this](int32 index)
				{
					if (m_glueScene && index >= 0 && index < GetNumCharacters())
					{
						EnsureOrder();
						PlazaAvatars& plaza = m_glueScene->GetPlazaAvatars();
						plaza.SetCharacters(GetOrderedCharacters());
						plaza.Leave(index);
					}
				}),

			// x, y in UI units and whether the character's nameplate is on screen.
			luabind::def<std::function<void(int32, float&, float&, bool&)>>("GetPlazaNameplatePosition", [this](int32 index, float& outX, float& outY, bool& outVisible)
				{
					outX = 0.0f;
					outY = 0.0f;
					outVisible = false;
					if (!m_glueScene)
					{
						return;
					}

					const std::optional<Vector3> anchor = m_glueScene->GetPlazaAvatars().GetNameplateAnchor(index);
					if (anchor)
					{
						outVisible = ProjectToUi(*anchor, outX, outY);
					}
				}, luabind::joined<luabind::pure_out_value<2>, luabind::pure_out_value<3>, luabind::pure_out_value<4>>()),

			// x, y in UI units of a point above an empty plaza spot, and whether it is on screen.
			luabind::def<std::function<void(int32, float&, float&, bool&)>>("GetPlazaSpotPosition", [this](int32 spot, float& outX, float& outY, bool& outVisible)
				{
					outX = 0.0f;
					outY = 0.0f;
					outVisible = false;
					if (!m_glueScene || spot < 0 || spot >= PlazaSpotCount)
					{
						return;
					}

					const Vector3 position = m_glueScene->GetDefinition().spots[spot].position + Vector3(0.0f, spotMarkerHeight, 0.0f);
					outVisible = ProjectToUi(position, outX, outY);
				}, luabind::joined<luabind::pure_out_value<2>, luabind::pure_out_value<3>, luabind::pure_out_value<4>>()),

			// Returns true if a flight started; GLUE_FLIGHT_FINISHED follows when it ends.
			luabind::def_lambda("GlueFlyToGate", [this]()
				{
					if (!m_glueScene)
					{
						return false;
					}

					GlueCameraDirector& director = m_glueScene->GetDirector();
					director.FlyOut();
					return director.GetMode() == GlueCameraMode::FlyingOut;
				}),

			// Returns true if a flight started; GLUE_FLIGHT_FINISHED follows when it ends.
			luabind::def_lambda("GlueFlyToPlaza", [this]()
				{
					if (!m_glueScene)
					{
						return false;
					}

					GlueCameraDirector& director = m_glueScene->GetDirector();
					director.FlyIn();
					return director.GetMode() == GlueCameraMode::FlyingIn;
				}),

			luabind::def_lambda("GlueSkipFlight", [this]()
				{
					if (m_glueScene)
					{
						m_glueScene->GetDirector().Skip();
					}
				}),

			luabind::def_lambda("IsGlueFlightActive", [this]()
				{
					return m_glueScene != nullptr && m_glueScene->IsFlightActive();
				}),

			// Returns true if the push-in started; GLUE_PUSHIN_FINISHED follows when it ends.
			luabind::def_lambda("GluePushIn", [this]()
				{
					if (!m_glueScene)
					{
						return false;
					}

					m_glueScene->PushIn(pushInSeconds);
					return true;
				})
		);
	}

	void CharSelect::SetGlueScene(GlueScene* scene)
	{
		// The plaza and every avatar on it belong to the scene: nothing of it is kept here, so
		// dropping the pointer is all that is needed when the scene goes away.
		m_glueScene = scene;
		m_plazaPlaced = false;
	}

	void CharSelect::OnCharacterListUpdated()
	{
		m_order.clear();
		EnsureOrder();

		if (m_glueScene)
		{
			// Characters that were sent away and are still listed were not deleted after all.
			m_glueScene->GetPlazaAvatars().ForgetLeaving();
		}
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
		plaza.SetCharacters(GetOrderedCharacters());

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

		EnsureOrder();
		return FindCharacter(m_order[index]);
	}

	bool CharSelect::IsCharacterDisabled(const int32 index) const
	{
		const CharacterView* view = GetCharacterView(index);
		if (!view)
		{
			return false;
		}

		return m_realmConnector.IsRaceDisabled(view->GetRaceId()) ||
			   m_realmConnector.IsClassDisabled(view->GetClassId());
	}

	void CharSelect::MoveCharacterInOrder(const int32 fromIndex, const int32 toIndex)
	{
		const int32 count = GetNumCharacters();
		if (fromIndex < 0 || fromIndex >= count || toIndex < 0 || toIndex >= count || fromIndex == toIndex)
		{
			return;
		}

		EnsureOrder();

		const uint64 selectedGuid = (m_selectedCharacter >= 0 && m_selectedCharacter < count) ? m_order[m_selectedCharacter] : 0;
		MoveRosterEntry(m_order, static_cast<size_t>(fromIndex), static_cast<size_t>(toIndex));

		// The selection follows its character.
		if (selectedGuid != 0)
		{
			const auto it = std::find(m_order.begin(), m_order.end(), selectedGuid);
			m_selectedCharacter = static_cast<int32>(it - m_order.begin());
		}

		if (!m_glueScene)
		{
			return;
		}

		// Characters dragged into or out of the top spots walk in or out right away.
		PlazaAvatars& plaza = m_glueScene->GetPlazaAvatars();
		plaza.SetCharacters(GetOrderedCharacters());
		if (m_selectedCharacter >= 0)
		{
			plaza.Select(m_selectedCharacter, m_plazaPlaced);
			m_plazaPlaced = true;
		}
	}

	void CharSelect::SaveCharacterOrder()
	{
		EnsureOrder();

		const auto& views = m_realmConnector.GetCharacterViews();
		bool changed = false;
		for (size_t i = 0; i < views.size(); ++i)
		{
			if (views[i].GetGuid() != m_order[i])
			{
				changed = true;
				break;
			}
		}

		if (changed)
		{
			// Also reorders the connector's list, so it and the local order agree again.
			m_realmConnector.SetCharacterOrder(m_order);
		}
	}

	int32 CharSelect::GetInitialCharacterIndex() const
	{
		EnsureOrder();

		uint64 lastGuid = 0;
		if (const ConsoleVar* var = ConsoleVarMgr::FindConsoleVar(lastCharactersVarName))
		{
			lastGuid = GetLastCharacterForRealm(var->GetStringValue(), m_realmConnector.GetRealmId());
		}

		return ResolveInitialSelection(m_order, lastGuid);
	}

	void CharSelect::SetLastCharacter(const int32 index)
	{
		const CharacterView* view = GetCharacterView(index);
		if (!view)
		{
			return;
		}

		ConsoleVar* var = ConsoleVarMgr::FindConsoleVar(lastCharactersVarName);
		if (!var)
		{
			return;
		}

		var->Set(SetLastCharacterForRealm(var->GetStringValue(), m_realmConnector.GetRealmId(), view->GetGuid()));
	}

	void CharSelect::EnsureOrder() const
	{
		const auto& views = m_realmConnector.GetCharacterViews();

		bool valid = m_order.size() == views.size();
		for (size_t i = 0; valid && i < m_order.size(); ++i)
		{
			valid = FindCharacter(m_order[i]) != nullptr;
		}

		if (valid)
		{
			return;
		}

		m_order.clear();
		m_order.reserve(views.size());
		for (const CharacterView& view : views)
		{
			m_order.push_back(view.GetGuid());
		}
	}

	const CharacterView* CharSelect::FindCharacter(const uint64 guid) const
	{
		const auto& views = m_realmConnector.GetCharacterViews();
		const auto it = std::find_if(views.begin(), views.end(), [guid](const CharacterView& view) { return view.GetGuid() == guid; });
		return it == views.end() ? nullptr : &*it;
	}

	std::vector<CharacterView> CharSelect::GetOrderedCharacters() const
	{
		EnsureOrder();

		std::vector<CharacterView> ordered;
		ordered.reserve(m_order.size());
		for (const uint64 guid : m_order)
		{
			if (const CharacterView* view = FindCharacter(guid))
			{
				ordered.push_back(*view);
			}
		}

		return ordered;
	}

	bool CharSelect::ProjectToUi(const Vector3& worldPosition, float& x, float& y) const
	{
		float normalizedX = 0.0f;
		float normalizedY = 0.0f;
		if (!m_glueScene || !m_glueScene->ProjectToScreen(worldPosition, normalizedX, normalizedY))
		{
			return false;
		}

		// The scene fills the screen, and screen px / uiScale = UI units on each axis, so a
		// normalized position times the native resolution is already in UI units.
		const Size& native = FrameManager::Get().GetNativeResolution();
		x = normalizedX * native.width;
		y = normalizedY * native.height;
		return true;
	}

	String CharSelect::GetCharacterZoneName(const int32 index) const
	{
		const CharacterView* view = GetCharacterView(index);
		if (!view)
		{
			return String();
		}

		const proto_client::ZoneEntry* zone = m_project.zones.getById(view->GetZoneId());
		return zone ? zone->name() : String();
	}
}
