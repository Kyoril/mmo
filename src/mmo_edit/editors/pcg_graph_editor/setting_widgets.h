// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "pcg/graph/graph_data.h"

#include <set>
#include <vector>

namespace mmo
{
	/// @brief One entry of a weighted asset list value ("path|weight;path|weight").
	struct WeightedAsset
	{
		String asset;
		float weight = 1.0f;
	};

	/// @brief Parses a weighted asset list ("path|weight;path|weight"). A missing or invalid weight is 1; empty
	///        entries are skipped.
	std::vector<WeightedAsset> ParseWeightedAssetList(const String& text);

	/// @brief Writes a weighted asset list ("path|weight;path|weight").
	String WriteWeightedAssetList(const std::vector<WeightedAsset>& entries);

	/// @brief Gets the file extensions of an "asset:<ext;ext>" or "assetList:<ext;ext>" widget hint (empty for
	///        any other hint).
	std::set<String> ParseWidgetExtensions(const String& widget);

	/// @brief Draws a weighted asset list as rows of asset picker, weight and remove button, plus an add button.
	/// @param value The list text, rewritten when anything changes.
	/// @param extensions The asset extensions the pickers offer.
	/// @return True if the value changed.
	bool DrawWeightedAssetList(String& value, const std::set<String>& extensions);

	/// @brief Draws an editor for one typed value, independent of where the value comes from (node setting,
	///        control property, parameter default).
	///
	///        Float and Int are drag fields (clamped to [min, max] when max > min); Bool is a checkbox; Vector3 is
	///        a colour picker for the "color" hint, else three drag fields; String is a combo when enumValues is
	///        not empty, an asset picker for "asset:<ext;ext>", a weighted asset list for "assetList:<ext;ext>",
	///        else a text field. A value of another type than `type` is shown (and replaced on edit) as the
	///        type's default.
	/// @param label ImGui label (also the visible text).
	/// @param value The value; written when the user edits it.
	/// @param type The value's type.
	/// @param enumValues Allowed values of an enum-like String (empty = free).
	/// @param min Lower bound of numeric values (used when max > min).
	/// @param max Upper bound of numeric values (used when max > min).
	/// @param widget Editor hint: "", "asset:<ext;ext>", "assetList:<ext;ext>" or "color".
	/// @return True if the value changed.
	bool DrawAttributeValueWidget(const char* label, pcg::graph::AttributeValue& value, pcg::graph::AttributeType type,
		const std::vector<String>& enumValues, float min, float max, const String& widget);
}
