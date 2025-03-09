
#pragma once

#include "model_frame.h"
#include "game/character_customization/customizable_avatar_definition.h"

namespace mmo
{
	/// This class renders a model inside it's content area.
	class UnitModelFrame final : public ModelFrame, public CustomizationPropertyGroupApplier
	{
	public:
		/// Default constructor.
		explicit UnitModelFrame(const std::string& name);

		~UnitModelFrame() override = default;

	public:
		void SetUnit(const std::string& unitName);

	public:
		void Apply(const VisibilitySetPropertyGroup& group, const AvatarConfiguration& configuration) override;
		void Apply(const MaterialOverridePropertyGroup& group, const AvatarConfiguration& configuration) override;
		void Apply(const ScalarParameterPropertyGroup& group, const AvatarConfiguration& configuration) override;
	};
}
