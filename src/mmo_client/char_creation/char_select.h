#pragma once

#include "base/non_copyable.h"
#include "game/character_view.h"
#include "game/character_customization/customizable_avatar_definition.h"
#include "game_client/item_display_applier.h"

#include <unordered_map>

struct lua_State;

namespace mmo
{
	class Entity;
	class TagPoint;
	class Frame;
	class ModelFrame;
	class RealmConnector;

	namespace proto_client
	{
		class ModelDataEntry;
		class Project;
	}

	class CharSelect final : public NonCopyable, public CustomizationPropertyGroupApplier
	{
	public:
		explicit CharSelect(const proto_client::Project& project, RealmConnector& realmConnector);

		~CharSelect() override = default;

	public:
		void RegisterScriptFunctions(lua_State* lua);

		void SetModelFrame(Frame* frame);

		void SelectCharacter(int32 index);

		int32 GetNumCharacters() const;

		const mmo::CharacterView* GetCharacterView(int32 index) const;

		int32 GetSelectedCharacter() const { return m_selectedCharacter; }

		/// Returns true when the character at the given index has a disabled race or class.
		[[nodiscard]] bool IsCharacterDisabled(int32 index) const;

	public:
		void Apply(const VisibilitySetPropertyGroup& group, const AvatarConfiguration& configuration) override;

		void Apply(const MaterialOverridePropertyGroup& group, const AvatarConfiguration& configuration) override;

		void Apply(const ScalarParameterPropertyGroup& group, const AvatarConfiguration& configuration) override;

	private:
		void ClearItemAttachments();

	private:
		const proto_client::Project& m_project;

		RealmConnector& m_realmConnector;

		ModelFrame* m_modelFrame = nullptr;

		int32 m_selectedCharacter = -1;

		ItemDisplayAttachmentMap m_itemAttachments;
	};
}
