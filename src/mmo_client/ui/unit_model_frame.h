// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "model_frame.h"
#include "game/character_customization/customizable_avatar_definition.h"
#include "game_client/item_display_applier.h"

namespace mmo
{
	class GameUnitC;

	/// This class renders a model inside it's content area.
	class UnitModelFrame final : public ModelFrame, public CustomizationPropertyGroupApplier
	{
	public:
		/// Default constructor.
		explicit UnitModelFrame(const std::string& name);

		~UnitModelFrame() override;

	public:
		/// Displays the given unit (by unit name, e.g. "player" or "target") including its
		/// customization options and the visuals of everything it has equipped. The frame keeps
		/// tracking that unit and refreshes itself whenever its equipment visuals change.
		void SetUnit(const std::string& unitName);

	public:
		void Apply(const VisibilitySetPropertyGroup& group, const AvatarConfiguration& configuration) override;
		void Apply(const MaterialOverridePropertyGroup& group, const AvatarConfiguration& configuration) override;
		void Apply(const ScalarParameterPropertyGroup& group, const AvatarConfiguration& configuration) override;

	protected:
		void OnModelFileChanged(const Property& prop) override;

	private:
		/// Re-resolves the tracked unit, subscribes to its equipment visuals and rebuilds the
		/// displayed model.
		void RefreshUnit();

		/// Rebuilds the displayed model from the currently tracked unit. Clears the model if that
		/// unit does not exist (anymore). Never touches the equipment subscription, so it is safe
		/// to call from the equipment change handler itself.
		void RefreshVisuals();

		/// Applies the item display data of everything the given unit has equipped to the
		/// displayed entity. Only players carry visible equipment.
		void ApplyEquipment(GameUnitC& unit);

		/// Detaches and destroys all item meshes attached to the displayed entity.
		void ClearItemAttachments();

	private:
		/// Name of the unit to display (e.g. "player" or "target").
		String m_unitName;

		/// Item meshes attached to the displayed entity, keyed by item display id.
		ItemDisplayAttachmentMap m_itemAttachments;

		/// Keeps the displayed model in sync with the equipment of the tracked unit.
		scoped_connection m_equipmentVisualsConnection;
	};
}
