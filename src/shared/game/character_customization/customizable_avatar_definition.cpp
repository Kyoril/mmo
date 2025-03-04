
#include "customizable_avatar_definition.h"

namespace mmo
{
	CustomizableAvatarDefinition::CustomizableAvatarDefinition(String baseMesh): m_baseMesh(std::move(baseMesh))
	{
	}

	void CustomizableAvatarDefinition::Apply(CharacterCustomizationPropertyVisitor& visitor) const
	{
	}

	void CustomizableAvatarDefinition::AddProperty(std::unique_ptr<CustomizationPropertyGroup> property)
	{
		m_properties.push_back(std::move(property));
	}

	bool CustomizableAvatarDefinition::OnReadFinished() noexcept
	{
		return ChunkReader::OnReadFinished();
	}
}
