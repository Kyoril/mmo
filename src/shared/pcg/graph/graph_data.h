// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "attribute_value.h"
#include "base/typedefs.h"
#include "math/vector3.h"
#include "math/quaternion.h"
#include "math/path_spline.h"
#include "terrain_op.h"

#include <map>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mmo::pcg::graph
{
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
		Params,
		Terrain
	};

	/// @brief A shape that cuts gaps into the props of a section (the Exclude input of the section nodes).
	struct ExclusionShape
	{
		/// @brief The shape: a closed loop for an area, else a spline.
		PathSpline spline;

		/// @brief True: positions inside the loop are excluded (radius unused). False: positions within radius of the
		///        spline are excluded.
		bool area = false;

		/// @brief Spline shapes: the total removal radius (distance setting plus the item's clearance), positive.
		float radius = 0.0f;
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

		/// @brief Per spline point: the stable point id from the producing control (empty if generated).
		std::vector<uint64> pointIds;

		/// @brief Used by Terrain items: what to do to the terrain. Geometry is in spline / points.
		TerrainOp op;

		/// @brief Used by Terrain items of section ops: shapes whose positions get no section props.
		std::vector<ExclusionShape> exclusions;
	};

	/// @brief The ordered list of items on a pin.
	using Collection = std::vector<DataItem>;

	/// @brief Order-sensitive, deterministic hash covering every field of every item.
	uint64 ContentHash(const Collection& c);
}
