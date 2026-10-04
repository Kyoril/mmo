// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <functional>

namespace mmo
{
	/// Resolves the mesh file a model data entry renders: the file itself for plain meshes, the
	/// avatar definition's base mesh for customizable (.char) models. Returns an empty string
	/// when the model is unknown or cannot be resolved.
	using ModelMeshResolver = std::function<String(uint32 modelId)>;

	/// Decides whether an item display variant applies to a character model.
	///
	/// A variant targets a model data id (0 = every model). Besides the exact id, a variant also
	/// applies to every other model which renders the same mesh: item displays are authored
	/// against the player models (e.g. "PLAYER - Human Male Model"), and an NPC model built from
	/// the same mesh has to pick up the same variant without duplicating it per NPC model.
	/// @param variantModel The model id the variant targets, 0 for all models.
	/// @param modelDisplayId The model id of the unit the display is applied to.
	/// @param resolveMesh Resolves a model id to its mesh file. May be empty, which restricts the
	///	       match to the exact model id.
	/// @return true if the variant has to be applied to the model.
	bool ItemDisplayVariantAppliesToModel(uint32 variantModel, uint32 modelDisplayId, const ModelMeshResolver& resolveMesh);
}
