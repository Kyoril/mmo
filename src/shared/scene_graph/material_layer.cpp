// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "material_layer.h"

#include "entity.h"
#include "sub_entity.h"
#include "graphics/material_instance.h"

namespace mmo
{
	bool ApplyMaterialLayer(Entity& entity, const String& subEntityName, const std::string_view layer, const MaterialPtr& source)
	{
		SubEntity* subEntity = entity.GetSubEntity(subEntityName);
		if (!subEntity || !source || layer.empty())
		{
			return false;
		}

		const MaterialPtr current = subEntity->GetMaterial();
		if (!current)
		{
			return false;
		}

		// Reuse the instance a previous layer created on this sub entity, so layers stack instead of
		// each one replacing the last. The name identifies our own instance: anything else (the mesh
		// default, a customization or override material) becomes the new instance's parent.
		const String instanceName = entity.GetName() + "_LAYERS_" + subEntityName;

		std::shared_ptr<MaterialInstance> instance;
		if (current->GetName() == instanceName)
		{
			instance = std::dynamic_pointer_cast<MaterialInstance>(current);
		}

		if (!instance)
		{
			instance = std::make_shared<MaterialInstance>(instanceName, current);
			subEntity->SetMaterial(instance);
		}

		const String prefix = String(layer) + "_";

		for (const auto& param : source->GetTextureParameters())
		{
			instance->SetTextureParameter(prefix + param.name, param.texture);
		}

		for (const auto& param : source->GetScalarParameters())
		{
			instance->SetScalarParameter(prefix + param.name, param.value);
		}

		for (const auto& param : source->GetVectorParameters())
		{
			instance->SetVectorParameter(prefix + param.name, param.value);
		}

		return true;
	}
}
