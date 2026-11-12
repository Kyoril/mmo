// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "char_select.h"
#include "character_avatar_builder.h"

#include "base/macros.h"
#include "console/console_var.h"
#include "net/realm_connector.h"

#include "client_data/project.h"
#include "client_data/zone_names.h"
#include "client_locale.h"
#include "frame_ui/frame_mgr.h"
#include "game_client/character_roster.h"
#include "scene_graph/entity.h"
#include "ui/model_frame.h"

#include "luabind_lambda.h"

#include <algorithm>

namespace mmo
{
	namespace
	{
		/// Name of the cvar remembering the last played character per realm.
		const char* const lastCharactersVarName = "lastCharacters";

		class CharacterPreviewFrame final : public ModelFrame
		{
		public:
			explicit CharacterPreviewFrame(const std::string& name)
				: ModelFrame(name)
			{
			}

			~CharacterPreviewFrame() override
			{
				ClearAvatar();
			}

			void SetCharacter(const proto_client::Project& project, const CharacterView* view)
			{
				ClearAvatar();
				if (view)
				{
					CharacterAvatarBuilder builder(project);
					m_entity = builder.Build(m_scene, *m_entityNode, *view, "SelectedCharacter", m_attachments);
				}

				if (m_entity && !GetRenderer())
				{
					SetRenderer("ModelRenderer");
				}
				RebindAnimationState();
				InvalidateModel();
				SetVisible(m_entity != nullptr);
			}

		private:
			void ClearAvatar()
			{
				if (m_entity)
				{
					ClearItemDisplayAttachments(m_scene, *m_entity, m_attachments);
					m_entityNode->DetachObject(*m_entity);
					m_scene.DestroyEntity(*m_entity);
					m_entity = nullptr;
					m_animationState = nullptr;
				}
			}

			ItemDisplayAttachmentMap m_attachments;
		};
	}

	void CharSelect::RegisterPreviewFrameFactory()
	{
		FrameManager::Get().RegisterFrameFactory("CharacterPreview", [](const std::string& name)
		{
			return std::make_shared<CharacterPreviewFrame>(name);
		});
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
			luabind::def_lambda("SetCharacterPreview", [this](Frame* frame, int32 index)
				{
					if (auto* preview = dynamic_cast<CharacterPreviewFrame*>(frame))
					{
						preview->SetCharacter(m_project, GetCharacterView(index));
					}
				}),
			luabind::def_lambda("GetSelectedCharacterIndex", [this]() { return GetSelectedCharacter(); }),
			luabind::def_lambda("IsCharacterDisabled", [this](int32 index) { return IsCharacterDisabled(index); }),
			luabind::def_lambda("GetCharacterZoneName", [this](int32 index) { return GetCharacterZoneName(index); }),
			luabind::def_lambda("GetInitialCharacterIndex", [this]() { return GetInitialCharacterIndex(); }),
			luabind::def_lambda("SetLastCharacter", [this](int32 index) { SetLastCharacter(index); }),
			luabind::def_lambda("MoveCharacterInOrder", [this](int32 fromIndex, int32 toIndex) { MoveCharacterInOrder(fromIndex, toIndex); }),
			luabind::def_lambda("SaveCharacterOrder", [this]() { SaveCharacterOrder(); }),

			// Turns the character shown by the character screen's preview (CharSelectPreview).
			luabind::def_lambda("RotateCharacter", [](float degrees)
				{
					if (const auto preview = FrameManager::Get().Find("CharSelectPreview"))
					{
						if (auto* model = dynamic_cast<CharacterPreviewFrame*>(preview.get()))
						{
							model->Yaw(degrees);
						}
					}
				})
		);
	}

	void CharSelect::OnCharacterListUpdated()
	{
		m_order.clear();
		EnsureOrder();
	}

	void CharSelect::SelectCharacter(const int32 index)
	{
		m_selectedCharacter = index;
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

	String CharSelect::GetCharacterZoneName(const int32 index) const
	{
		const CharacterView* view = GetCharacterView(index);
		if (!view)
		{
			return String();
		}

		const proto_client::ZoneEntry* zone = m_project.zones.getById(view->GetZoneId());
		return zone ? GetZoneDisplayName(*zone, GetClientLocale()) : String();
	}
}
