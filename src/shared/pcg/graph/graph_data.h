// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"
#include "math/quaternion.h"
#include "math/path_spline.h"

#include <map>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

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

	/// @brief One point of a point set.
	struct Point
	{
		Vector3 position;
		Quaternion rotation;
		Vector3 scale = Vector3(1, 1, 1);
		float density = 1.0f;
		uint64 seed = 0;
		Vector3 boundsMin = Vector3(-0.5f, -0.5f, -0.5f);
		Vector3 boundsMax = Vector3(0.5f, 0.5f, 0.5f);
	};

	/// @brief Points plus per-point attribute columns (each column has points.size() entries, one type).
	struct PointSet
	{
		std::vector<Point> points;
		std::map<String, std::pair<AttributeType, std::vector<AttributeValue>>> columns;

		/// @brief Sets a per-point attribute, creating the column (filled with type defaults) if needed.
		/// @return False if the index is out of range or the value's type differs from the column's type.
		bool SetAttribute(size_t index, const String& name, const AttributeValue& value);

		/// @brief Gets a per-point attribute, or nullptr if the column or index does not exist.
		const AttributeValue* GetAttribute(size_t index, const String& name) const;

		/// @brief Appends the points of another set. Columns are merged; entries a side does not have
		///        are filled with the type default. If both sides have a column of the same name but
		///        different types, this set's column wins and the other's values become defaults.
		void Append(const PointSet& other);

		/// @brief Filters the points in place, preserving order and columns. A point is kept if its
		///        mask entry is non-zero; points beyond the mask's size are dropped.
		void Keep(const std::vector<uint8>& keepMask);
	};

	/// @brief What a data item holds.
	enum class ItemKind : uint8
	{
		Points,
		Spline,
		Area,
		Params
	};

	/// @brief One item flowing along a graph link.
	struct DataItem
	{
		ItemKind kind = ItemKind::Params;
		std::set<String> tags;

		/// @brief Item-level attributes (control properties, etc.).
		AttributeSet attributes;

		/// @brief Used by Points items.
		PointSet points;

		/// @brief Used by Spline (open or closed) and Area (closed loop) items.
		PathSpline spline;

		/// @brief Producing control id (0 = generated).
		uint64 sourceId = 0;
	};

	/// @brief The ordered list of items on a pin.
	using Collection = std::vector<DataItem>;

	/// @brief Order-sensitive, deterministic hash covering every field of every item.
	uint64 ContentHash(const Collection& c);
}
