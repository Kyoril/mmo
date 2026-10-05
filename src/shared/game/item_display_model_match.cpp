// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "item_display_model_match.h"

namespace mmo
{
	bool ItemDisplayVariantAppliesToModel(const uint32 variantModel, const uint32 modelDisplayId, const ModelMeshResolver& resolveMesh)
	{
		if (variantModel == 0 || variantModel == modelDisplayId)
		{
			return true;
		}

		if (!resolveMesh)
		{
			return false;
		}

		const String variantMesh = resolveMesh(variantModel);
		if (variantMesh.empty())
		{
			return false;
		}

		return variantMesh == resolveMesh(modelDisplayId);
	}
}
