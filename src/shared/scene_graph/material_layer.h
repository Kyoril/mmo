// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "graphics/material.h"

#include <string_view>

namespace mmo
{
	class Entity;

	/// Composites a material onto one of a layered material's named layer slots, instead of replacing
	/// the sub entity's material. Used for clothing painted in the body's UV layout: the body material
	/// (skin, chosen by character customization) declares layer slots like "Legs_BaseColor", and the
	/// clothing material fills them, so skin stays visible wherever the clothing texture is transparent.
	///
	/// On first use the sub entity gets a per-entity MaterialInstance parented to its current material;
	/// later layers on the same sub entity reuse that instance, so layers stack. Every texture, scalar
	/// and vector parameter P of the source material is written to the instance parameter
	/// "<layer>_P". Parameters the target does not declare are skipped silently, which makes a layer on
	/// a material without that slot a no-op rather than an error. Resetting the sub entity's material
	/// discards the instance and with it every applied layer.
	///
	/// @param entity The entity owning the sub entity.
	/// @param subEntityName Name of the sub entity to apply the layer to. Unknown names are ignored.
	/// @param layer Name of the layer slot in the target material, e.g. "Legs".
	/// @param source The material providing the layer's parameter values.
	/// @return true if the layer was applied to an existing sub entity.
	bool ApplyMaterialLayer(Entity& entity, const String& subEntityName, std::string_view layer, const MaterialPtr& source);
}
