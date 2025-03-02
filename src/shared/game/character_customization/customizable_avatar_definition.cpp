
#include "customizable_avatar_definition.h"

namespace mmo
{
	CustomizableAvatarDefinition::CustomizableAvatarDefinition(String baseMesh): m_baseMesh(std::move(baseMesh))
	{
	}

	void CustomizableAvatarDefinition::Apply(CharacterCustomizationPropertyVisitor& visitor) const
	{
		for (auto& property : m_properties)
		{
			property->Accept(visitor);
		}
	}

	bool CustomizableAvatarDefinition::OnReadFinished() noexcept
	{
		return ChunkReader::OnReadFinished();
	}
}
