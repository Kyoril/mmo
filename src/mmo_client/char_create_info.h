// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "frame_ui/frame.h"
#include "game/character_customization/customizable_avatar_definition.h"
#include "ui/model_frame.h"

namespace mmo
{
	class RealmConnector;

	namespace proto_client
	{
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
		void ResetCharacterCreation();

		void SetCharacterCreationFrame(Frame* frame);

		void SetSelectedRace(int32 raceId);

		void SetSelectedClass(int32 classId);

		void SetSelectedGender(int32 gender);

		[[nodiscard]] int32 GetSelectedRace() const;

		[[nodiscard]] int32 GetSelectedClass() const;

		[[nodiscard]] int32 GetSelectedGender() const;

		const std::vector<String>& GetPropertyNames() const { return m_propertyNameCache; }

		void CycleCustomizationProperty(const String& propertyName, bool forward, bool apply);

		void CreateCharacter(const String& name) const;

	private:
		void RefreshModel();

		void ApplyCustomizations();

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
	};
}
