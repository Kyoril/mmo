// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "pcg/graph/controls.h"

namespace mmo
{
	/// @brief Typed property-override editor for one placed control: one section per property group,
	///        one widget per property (showing the override, else the default), and a reset-to-default
	///        button next to every overridden property. Editing a value stores it as an override.
	/// @param type The control type of the instance.
	/// @param instance The placed control whose overrides are edited.
	/// @return True if anything changed.
	bool DrawControlProperties(const pcg::graph::ControlType& type, pcg::graph::ControlInstance& instance);

	/// @brief Draws the widget of one property for a value: by type a drag field, checkbox, combo (Enum), asset
	///        picker, weighted asset list or colour picker, using the property's range and enum values.
	/// @param label ImGui label (also the visible text).
	/// @param property The property definition (type, range, enum values).
	/// @param value The value; written when the user edits it.
	/// @return True if the value changed.
	bool DrawControlPropertyWidget(const char* label, const pcg::graph::PropertyDef& property, pcg::graph::AttributeValue& value);

	/// @brief Finds a property of a control type by name.
	/// @return The property, or null if the type has none of that name.
	const pcg::graph::PropertyDef* FindControlProperty(const pcg::graph::ControlType& type, const String& name);

	/// @brief Parses a property value from text: Float and Int as numbers, Bool as true/false/1/0, Enum as one
	///        of the allowed values, Color as "r,g,b", String, Asset and AssetList ("asset|weight;...") as is.
	/// @param property The property the value is for.
	/// @param text The text to parse.
	/// @param out Receives the value on success.
	/// @param error Receives the reason on failure.
	/// @return True if the text is a valid value of the property.
	bool ParseControlPropertyValue(const pcg::graph::PropertyDef& property, const String& text, pcg::graph::AttributeValue& out, String& error);
}
