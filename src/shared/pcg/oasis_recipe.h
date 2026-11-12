// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "oasis.h"

namespace mmo::pcg
{
	/// Parses the oasis recipe JSON (data/client/Editor/Pcg/OasisRecipe.json). OasisParams fields are
	/// read by their C++ name (top level or inside a "params" object); asset lists and material names
	/// (cliffs, shelves, clumps, palms, bushes, waterPlants, terrainMaterial, waterMaterial) at the top
	/// level or inside an "assets" object. Missing keys keep the values passed in. The parameters are sanitized (SanitizeParams) after parsing.
	/// @return False if the text is not a valid JSON object.
	bool ParseOasisRecipe(const String& jsonText, OasisParams& params, OasisAssets& assets);
}
