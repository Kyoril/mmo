// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "frame_ui/frame.h"
#include "game/character_customization/customizable_avatar_definition.h"
#include "game_client/item_display_applier.h"
#include "ui/model_frame.h"

struct lua_State;

namespace mmo
{
	class RealmConnector;

	namespace proto_client
	{
		class CharacterOutfit;
		class ModelDataEntry;
		class Project;
	}

	/// This class manages the character creation state info in the login screen.
	class CharCreateInfo final : public NonCopyable, public CustomizationPropertyGroupApplier
	{
	public:
		explicit CharCreateInfo(const proto_client::Project& project, RealmConnector& realmConnector);

		~CharCreateInfo() override = default;

	public:
		void RegisterScriptFunctions(lua_State* lua);

		void ResetCharacterCreation();

		void SetCharacterCreationFrame(Frame* frame);

		void SetSelectedRace(int32 raceId);

		void SetSelectedClass(int32 classId);

		void SetSelectedGender(int32 gender);

		[[nodiscard]] int32 GetSelectedRace() const;

		[[nodiscard]] int32 GetSelectedClass() const;

		[[nodiscard]] int32 GetSelectedGender() const;

		/// Shows or hides the cosmetic creation outfit. Hiding rebuilds the preview model, which
		///	restores body parts the outfit's item displays had hidden.
		void SetOutfitVisible(bool visible);

		/// Returns whether the cosmetic creation outfit is currently shown.
		[[nodiscard]] bool IsOutfitVisible() const { return m_outfitVisible; }

		const std::vector<String>& GetPropertyNames() const { return m_propertyNameCache; }

		void CycleCustomizationProperty(const String& propertyName, bool forward, bool apply);

		void CreateCharacter(const String& name) const;

		const char* GetCustomizationValue(const String& propertyName) const;

		/// Returns true when the given race is not disabled on the current realm.
		[[nodiscard]] bool IsRaceAvailable(int32 raceId) const;

		/// Returns true when the given class is not disabled on the current realm.
		[[nodiscard]] bool IsClassAvailable(int32 classId) const;

		/// Returns true when at least one race and one class are available (character creation is possible).
		[[nodiscard]] bool IsCharacterCreationAvailable() const;

		/// Returns the total number of races defined in the project data.
		[[nodiscard]] int32 GetNumRaces() const;

		/// Returns the race ID at the given proto-list index, or -1 if out of range.
		[[nodiscard]] int32 GetRaceId(int32 index) const;

		/// Returns the display name of the race at the given proto-list index, or nullptr if out of range.
		[[nodiscard]] const char* GetRaceName(int32 index) const;

		/// Returns the total number of classes defined in the project data.
		[[nodiscard]] int32 GetNumClasses() const;

		/// Returns the class ID at the given proto-list index, or -1 if out of range.
		[[nodiscard]] int32 GetClassId(int32 index) const;

		/// Returns the display name of the class at the given proto-list index, or nullptr if out of range.
		[[nodiscard]] const char* GetClassEntryName(int32 index) const;

		/// Returns the display name of the class with the given ID, or nullptr if not found.
		[[nodiscard]] const char* GetClassNameById(int32 classId) const;

		/// Returns the display name of the race with the given ID, or nullptr if not found.
		[[nodiscard]] const char* GetRaceNameById(int32 raceId) const;

	private:
		void RefreshModel();

		void ApplyCustomizations();

		/// Returns the outfit which should currently be shown, or nullptr when the outfit is
		///	hidden or no outfit of the selected class matches the selected race and gender.
		[[nodiscard]] const proto_client::CharacterOutfit* FindOutfit() const;

		/// Attaches and applies every item display of the current outfit to the preview entity.
		///	Does nothing when the outfit is hidden or no outfit matches.
		void ApplyOutfit();

		/// Detaches and destroys every item mesh attached to the preview entity.
		void ClearItemAttachments();

	public:
		void Apply(const VisibilitySetPropertyGroup& group, const AvatarConfiguration& configuration) override;
		void Apply(const MaterialOverridePropertyGroup& group, const AvatarConfiguration& configuration) override;
		void Apply(const ScalarParameterPropertyGroup& group, const AvatarConfiguration& configuration) override;

	private:
		const proto_client::Project& m_project;

		RealmConnector& m_realmConnector;

		ModelFrame* m_characterCreationFrame = nullptr;

		int32 m_selectedRace = 0;

		int32 m_selectedClass = 0;

		int32 m_selectedGender = 0;

		bool m_modelChanged = true;

		const proto_client::ModelDataEntry* m_selectedModel = nullptr;

		std::shared_ptr<CustomizableAvatarDefinition> m_avatarDefinition;

		AvatarConfiguration m_configuration;

		scoped_connection m_frameConnection;

		std::vector<String> m_propertyNameCache;

		ItemDisplayAttachmentMap m_itemAttachments;

		bool m_outfitVisible = true;

		String m_defaultAnimation;
	};
}
