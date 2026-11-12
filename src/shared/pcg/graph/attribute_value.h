// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"

#include <map>
#include <string>
#include <variant>

namespace mmo::pcg::graph
{
	/// @brief Type of an attribute value.
	enum class AttributeType : uint8
	{
		Float,
		Int,
		Bool,
		Vector3,
		String
	};

	/// @brief A single typed attribute value. The variant index equals the AttributeType value.
	using AttributeValue = std::variant<float, int64, bool, Vector3, String>;

	/// @brief Gets the type of an attribute value.
	AttributeType TypeOf(const AttributeValue& v);

	/// @brief Gets the default (zero) value of an attribute type.
	AttributeValue DefaultValue(AttributeType type);

	/// @brief Item-level attributes and graph parameters. Ordered map: iteration order is deterministic.
	using AttributeSet = std::map<String, AttributeValue>;
}
