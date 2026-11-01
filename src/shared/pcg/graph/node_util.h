// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "graph_data.h"
#include "node_registry.h"

#include "math/path_spline.h"
#include "math/quaternion.h"
#include "math/vector3.h"

#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace mmo::pcg::graph
{
	/// @brief Adds a warning to a node output unless the same text was already reported.
	void WarnOnce(NodeOutput& out, const String& message);

	/// @brief Gets a numeric setting (Float or Int) as float, or the fallback if it is missing, of another
	///        type or not finite.
	float SettingFloat(const AttributeSet& settings, const String& name, float fallback);

	/// @brief Gets a Bool setting, or the fallback if it is missing or of another type.
	bool SettingBool(const AttributeSet& settings, const String& name, bool fallback);

	/// @brief The `stackOrder` of a terrain item: the item attribute of that name if it is numeric, else the setting
	///        (default 0), rounded and clamped to -1000..1000. A non-numeric attribute is ignored with a warning.
	int32 ResolveStackOrder(NodeOutput& out, const DataItem& source, const AttributeSet& settings);

	/// @brief The setting every node that emits Terrain items has (Int, 0, range -1000..1000).
	SettingDef StackOrderSetting();

	/// @brief Gets a String setting, or the fallback if it is missing or of another type.
	String SettingString(const AttributeSet& settings, const String& name, const String& fallback);

	/// @brief Splits a comma separated list, trimming spaces and tabs around entries and dropping empty ones.
	std::vector<String> SplitList(const String& list);

	/// @brief An axis-aligned rectangle on the x/z plane.
	struct Bounds2D
	{
		float minX = 0;
		float minZ = 0;
		float maxX = 0;
		float maxZ = 0;

		/// @brief False for a default constructed (empty) rectangle.
		bool valid = false;

		/// @brief Grows the rectangle to contain a point.
		void Add(float x, float z);

		/// @brief Whether two valid rectangles overlap (touching counts).
		bool Intersects(const Bounds2D& other) const;
	};

	/// @brief Conservative x/z bounds of a spline without evaluating it: the control points plus, for curved
	///        splines, their effective tangent handles (a Bezier segment lies inside the hull of its points
	///        and handles). Invalid if the spline has no points.
	Bounds2D SplineBounds(const PathSpline& spline);

	/// @brief Largest coordinate magnitude the nodes process; geometry beyond it (or non-finite) is skipped
	///        with a warning so cell loops and spline evaluation stay bounded.
	constexpr float MaxWorldCoordinate = 1.0e7f;

	/// @brief Whether valid bounds are finite and within MaxWorldCoordinate.
	bool SaneBounds(const Bounds2D& bounds);

	/// @brief Finest polyline step used to evaluate splines for sampling.
	constexpr float SamplingStep = 0.25f;

	/// @brief Gets the polyline step to evaluate a spline with: the preferred step (at least SamplingStep),
	///        doubled until the polyline has at most about 100000 samples (doubling keeps the step stable
	///        while a vertex moves, which preserves locality).
	float EvaluationStep(const PathSpline& spline, float preferred);

	/// @brief A closed polyline with its segments bucketed into horizontal rows, for fast containment tests of
	///        many points. Contains uses the same segment predicate and intersection formula as
	///        SampledPath::Contains (even-odd rule), so both agree exactly.
	class RowIndexedPolygon
	{
	public:
		/// @brief Buckets the segments of a closed polyline (the closing segment included).
		/// @param polygon Polyline samples; fewer than 3 make an empty polygon.
		/// @param rowHeight Height of a row; must be positive.
		RowIndexedPolygon(const std::vector<SplineSample>& polygon, float rowHeight);

		/// @brief Even-odd containment test.
		bool Contains(float x, float z) const;

	private:
		std::vector<SplineSample> m_polygon;
		std::vector<std::vector<uint32>> m_rows;
		int64 m_firstRow = 0;
		float m_rowHeight = 1.0f;
	};

	/// @brief Rotation that maps local +Y onto a normal (identity for a zero or non-finite normal).
	Quaternion RotationOntoNormal(Vector3 normal);

	/// @brief Whether a name refers to a point property instead of an attribute: density, position.x,
	///        position.y, position.z, scale (reads scale.x, writes all three components), scale.x, scale.y,
	///        scale.z. Filter by Attribute, Attribute Math, Copy Attributes from Control and Distance To treat
	///        these names as the point property everywhere, even if a column of that name exists.
	bool IsReservedPointName(const String& name);

	/// @brief Converts a Float, Int or Bool value to float (Bool: 0 or 1). False for other types or non-finite values.
	bool ToNumber(const AttributeValue& value, float& out);

	/// @brief Looks up a value for one point: the point property for a reserved name, else the point's column,
	///        else the item attribute.
	/// @return False if none of them exists.
	bool ReadPointValue(const DataItem& item, size_t index, const String& name, AttributeValue& out);

	/// @brief Writes a number for one point: the point property for a reserved name, else a Float column
	///        (created if missing, filled with 0 for the other points).
	/// @return False if the column exists with another type (nothing is written).
	bool WritePointNumber(DataItem& item, size_t index, const String& name, float value);

	/// @brief Nearest-distance queries against a polyline on the x/z plane, with its segments bucketed into a
	///        uniform grid. Results do not depend on the bucketing (the minimum over segments is exact).
	class SegmentDistanceIndex
	{
	public:
		/// @brief Buckets the polyline's segments (and the closing segment when closed).
		/// @param polyline Polyline samples; a single sample acts as a point, none make an empty index.
		/// @param closed Whether the last sample connects back to the first.
		/// @param cellSize Preferred grid cell size; grown so the grid has at most 128 x 128 cells.
		SegmentDistanceIndex(const std::vector<SplineSample>& polyline, bool closed, float cellSize);

		/// @brief Finds the distance from (x, z) to the nearest segment if it is at most maxDistance.
		/// @return False if the index is empty or no segment is within maxDistance.
		bool Nearest(float x, float z, float maxDistance, float& distance) const;

		/// @brief Bounds of the polyline (invalid when empty).
		const Bounds2D& GetBounds() const
		{
			return m_bounds;
		}

	private:
		/// @brief Distance from (x, z) to a segment.
		double SegmentDistance(uint32 segment, double x, double z) const;

	private:
		std::vector<SplineSample> m_samples;
		std::vector<std::pair<uint32, uint32>> m_segments;
		std::vector<std::vector<uint32>> m_cells;
		Bounds2D m_bounds;
		double m_cellSize = 1.0;
		double m_originX = 0.0;
		double m_originZ = 0.0;
		int64 m_columns = 0;
		int64 m_rows = 0;
	};

	/// @brief Gets the input collection of a pin, or null when the pin has no link.
	const Collection* FindInput(const std::map<String, Collection>& inputs, const char* pin);

	/// @brief The standard required, multi-link "In" pin accepting Points.
	PinDef PointsInputPin();

	/// @brief Preferred grid cell size of the distance indices built by PrepareControl.
	constexpr float ControlDistanceCell = 4.0f;

	/// @brief An Area or Spline item prepared for containment and distance queries.
	struct PreparedControl
	{
		/// @brief The prepared item (not owned; must outlive this object).
		const DataItem* item = nullptr;

		/// @brief Containment index (Areas only, null for Splines).
		std::unique_ptr<RowIndexedPolygon> polygon;

		/// @brief Distance index over the spline curve, or over the area's edge (including the closing segment).
		std::unique_ptr<SegmentDistanceIndex> curve;
	};

	/// @brief Prepares an Area (at least 3 points) or Spline (at least 1 point) item: evaluates it with
	///        EvaluationStep(spline, SamplingStep), builds the containment index for Areas (row height
	///        max(1, z extent / 4096)) and the distance index with ControlDistanceCell.
	/// @return False (nothing prepared) for other kinds, too few points, non-finite or out of range geometry
	///         (warning) and Areas whose spline is not closed (warning).
	bool PrepareControl(NodeOutput& out, const DataItem& item, PreparedControl& prepared);
}
