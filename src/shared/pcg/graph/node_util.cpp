// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "node_util.h"

#include "math/constants.h"
#include "math/radian.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mmo::pcg::graph
{
	void WarnOnce(NodeOutput& out, const String& message)
	{
		if (std::find(out.warnings.begin(), out.warnings.end(), message) == out.warnings.end())
		{
			out.warnings.push_back(message);
		}
	}

	float SettingFloat(const AttributeSet& settings, const String& name, const float fallback)
	{
		const auto it = settings.find(name);
		if (it == settings.end())
		{
			return fallback;
		}

		float value = fallback;
		if (const float* f = std::get_if<float>(&it->second))
		{
			value = *f;
		}
		else if (const int64* i = std::get_if<int64>(&it->second))
		{
			value = static_cast<float>(*i);
		}
		return std::isfinite(value) ? value : fallback;
	}

	int32 ResolveStackOrder(NodeOutput& out, const DataItem& source, const AttributeSet& settings)
	{
		float value = SettingFloat(settings, "stackOrder", 0.0f);
		const auto it = source.attributes.find("stackOrder");
		if (it != source.attributes.end())
		{
			float fromItem = 0.0f;
			if (!std::holds_alternative<bool>(it->second) && ToNumber(it->second, fromItem))
			{
				value = fromItem;
			}
			else
			{
				WarnOnce(out, "ignored the item attribute 'stackOrder': it is not a number");
			}
		}
		return static_cast<int32>(std::lround(std::min(1000.0f, std::max(-1000.0f, value))));
	}

	SettingDef StackOrderSetting()
	{
		return SettingDef{ "stackOrder", static_cast<int64>(0), -1000.0f, 1000.0f, {}, "Lower values are stacked first; equal values keep node order" };
	}

	bool SettingBool(const AttributeSet& settings, const String& name, const bool fallback)
	{
		const auto it = settings.find(name);
		if (it == settings.end())
		{
			return fallback;
		}
		const bool* b = std::get_if<bool>(&it->second);
		return b != nullptr ? *b : fallback;
	}

	String SettingString(const AttributeSet& settings, const String& name, const String& fallback)
	{
		const auto it = settings.find(name);
		if (it == settings.end())
		{
			return fallback;
		}
		const String* s = std::get_if<String>(&it->second);
		return s != nullptr ? *s : fallback;
	}

	std::vector<String> SplitList(const String& list)
	{
		std::vector<String> result;
		size_t start = 0;
		while (start <= list.size())
		{
			size_t end = list.find(',', start);
			if (end == String::npos)
			{
				end = list.size();
			}

			size_t a = start;
			size_t b = end;
			while (a < b && (list[a] == ' ' || list[a] == '\t'))
			{
				++a;
			}
			while (b > a && (list[b - 1] == ' ' || list[b - 1] == '\t'))
			{
				--b;
			}
			if (b > a)
			{
				result.push_back(list.substr(a, b - a));
			}
			start = end + 1;
		}
		return result;
	}

	void Bounds2D::Add(const float x, const float z)
	{
		if (!valid)
		{
			minX = maxX = x;
			minZ = maxZ = z;
			valid = true;
			return;
		}
		minX = std::min(minX, x);
		maxX = std::max(maxX, x);
		minZ = std::min(minZ, z);
		maxZ = std::max(maxZ, z);
	}

	bool Bounds2D::Intersects(const Bounds2D& other) const
	{
		return valid && other.valid && minX <= other.maxX && other.minX <= maxX && minZ <= other.maxZ && other.minZ <= maxZ;
	}

	Bounds2D SplineBounds(const PathSpline& spline)
	{
		Bounds2D bounds;
		const bool curved = spline.interpolation == SplineInterpolation::CatmullRom;
		for (size_t i = 0; i < spline.points.size(); ++i)
		{
			const SplinePoint& p = spline.points[i];
			bounds.Add(p.x, p.z);
			if (curved)
			{
				float inX = 0, inZ = 0, outX = 0, outZ = 0;
				GetEffectiveTangents(spline, i, inX, inZ, outX, outZ);
				bounds.Add(p.x + inX, p.z + inZ);
				bounds.Add(p.x + outX, p.z + outZ);
			}
		}
		return bounds;
	}

	bool SaneBounds(const Bounds2D& bounds)
	{
		if (!bounds.valid)
		{
			return false;
		}
		for (const float v : { bounds.minX, bounds.minZ, bounds.maxX, bounds.maxZ })
		{
			if (!std::isfinite(v) || std::fabs(v) > MaxWorldCoordinate)
			{
				return false;
			}
		}
		return true;
	}

	float EvaluationStep(const PathSpline& spline, const float preferred)
	{
		float step = std::isfinite(preferred) ? std::max(preferred, SamplingStep) : SamplingStep;
		double chords = 0.0;
		const size_t n = spline.points.size();
		for (size_t i = 0; i + 1 < n || (spline.closed && n >= 3 && i < n); ++i)
		{
			const SplinePoint& a = spline.points[i];
			const SplinePoint& b = spline.points[(i + 1) % n];
			chords += std::hypot(static_cast<double>(b.x) - a.x, static_cast<double>(b.z) - a.z);
		}
		// Curved segments are longer than their chords; the factor 2 leaves room for that.
		const double coarse = chords * 2.0 / 100000.0;
		// Doubling (instead of using `coarse` directly) keeps the step, and so the polyline of unchanged segments,
		// stable while a vertex moves; it only changes when the spline's size crosses a power of two.
		while (std::isfinite(coarse) && coarse > step && step < MaxWorldCoordinate)
		{
			step *= 2.0f;
		}
		return step;
	}

	namespace
	{
		int64 RowOf(const float z, const float rowHeight)
		{
			return static_cast<int64>(std::floor(static_cast<double>(z) / static_cast<double>(rowHeight)));
		}
	}

	RowIndexedPolygon::RowIndexedPolygon(const std::vector<SplineSample>& polygon, const float rowHeight)
		: m_rowHeight(rowHeight > 0.0f ? rowHeight : 1.0f)
	{
		if (polygon.size() < 3)
		{
			return;
		}
		m_polygon = polygon;

		int64 lo = RowOf(polygon[0].z, m_rowHeight);
		int64 hi = lo;
		for (const SplineSample& s : polygon)
		{
			const int64 row = RowOf(s.z, m_rowHeight);
			lo = std::min(lo, row);
			hi = std::max(hi, row);
		}
		m_firstRow = lo;
		m_rows.resize(static_cast<size_t>(hi - lo + 1));

		const size_t n = m_polygon.size();
		for (size_t i = 0; i < n; ++i)
		{
			const SplineSample& a = m_polygon[i];
			const SplineSample& b = m_polygon[(i + 1) % n];
			const int64 r0 = RowOf(std::min(a.z, b.z), m_rowHeight);
			const int64 r1 = RowOf(std::max(a.z, b.z), m_rowHeight);
			for (int64 row = r0; row <= r1; ++row)
			{
				m_rows[static_cast<size_t>(row - m_firstRow)].push_back(static_cast<uint32>(i));
			}
		}
	}

	bool RowIndexedPolygon::Contains(const float x, const float z) const
	{
		if (m_rows.empty())
		{
			return false;
		}
		const int64 row = RowOf(z, m_rowHeight);
		if (row < m_firstRow || row >= m_firstRow + static_cast<int64>(m_rows.size()))
		{
			return false;
		}

		// A segment crossing the line at z spans z, so it is in z's row bucket.
		bool inside = false;
		const size_t n = m_polygon.size();
		for (const uint32 i : m_rows[static_cast<size_t>(row - m_firstRow)])
		{
			const SplineSample& a = m_polygon[i];
			const SplineSample& b = m_polygon[(i + 1) % n];
			if ((a.z > z) != (b.z > z))
			{
				const float crossX = a.x + (z - a.z) / (b.z - a.z) * (b.x - a.x);
				if (x < crossX)
				{
					inside = !inside;
				}
			}
		}
		return inside;
	}

	Quaternion RotationOntoNormal(Vector3 normal)
	{
		const float len = normal.GetLength();
		if (!(len > 1.0e-6f) || !std::isfinite(len))
		{
			return Quaternion::Identity;
		}
		normal = normal * (1.0f / len);
		const float d = std::clamp(Vector3::UnitY.Dot(normal), -1.0f, 1.0f);
		if (d > 0.999999f)
		{
			return Quaternion::Identity;
		}
		if (d < -0.999999f)
		{
			return Quaternion(Radian(Pi), Vector3::UnitX);
		}
		Vector3 axis = Vector3::UnitY.Cross(normal);
		axis.Normalize();
		return Quaternion(Radian(std::acos(d)), axis);
	}

	namespace
	{
		/// Pointer to the float a reserved point name refers to, or null ("scale" maps to scale.x). PointT is Point or
		/// const Point, so the mapping exists once for both overloads below.
		template <typename PointT>
		auto ReservedFieldOf(PointT& p, const String& name) -> decltype(&p.density)
		{
			if (name == "density")
			{
				return &p.density;
			}
			if (name == "position.x")
			{
				return &p.position.x;
			}
			if (name == "position.y")
			{
				return &p.position.y;
			}
			if (name == "position.z")
			{
				return &p.position.z;
			}
			if (name == "scale" || name == "scale.x")
			{
				return &p.scale.x;
			}
			if (name == "scale.y")
			{
				return &p.scale.y;
			}
			if (name == "scale.z")
			{
				return &p.scale.z;
			}
			return nullptr;
		}

		/// Writable reserved field of a point, or null.
		float* ReservedField(Point& p, const String& name)
		{
			return ReservedFieldOf(p, name);
		}

		/// Read-only reserved field of a point, or null.
		const float* ReservedField(const Point& p, const String& name)
		{
			return ReservedFieldOf(p, name);
		}
	}

	bool IsReservedPointName(const String& name)
	{
		const Point probe;
		return ReservedField(probe, name) != nullptr;
	}

	bool ToNumber(const AttributeValue& value, float& out)
	{
		float v = 0.0f;
		if (const float* f = std::get_if<float>(&value))
		{
			v = *f;
		}
		else if (const int64* i = std::get_if<int64>(&value))
		{
			v = static_cast<float>(*i);
		}
		else if (const bool* b = std::get_if<bool>(&value))
		{
			v = *b ? 1.0f : 0.0f;
		}
		else
		{
			return false;
		}
		if (!std::isfinite(v))
		{
			return false;
		}
		out = v;
		return true;
	}

	bool ReadPointValue(const DataItem& item, const size_t index, const String& name, AttributeValue& out)
	{
		if (index < item.points.points.size())
		{
			if (const float* field = ReservedField(item.points.points[index], name))
			{
				out = *field;
				return true;
			}
			if (const AttributeValue* v = item.points.GetAttribute(index, name))
			{
				out = *v;
				return true;
			}
		}
		const auto it = item.attributes.find(name);
		if (it == item.attributes.end())
		{
			return false;
		}
		out = it->second;
		return true;
	}

	bool WritePointNumber(DataItem& item, const size_t index, const String& name, const float value)
	{
		if (index >= item.points.points.size())
		{
			return false;
		}
		Point& p = item.points.points[index];
		if (name == "scale")
		{
			p.scale = Vector3(value, value, value);
			return true;
		}
		if (float* field = ReservedField(p, name))
		{
			*field = value;
			return true;
		}
		const auto column = item.points.columns.find(name);
		if (column != item.points.columns.end() && column->second.first != AttributeType::Float)
		{
			return false;
		}
		return item.points.SetAttribute(index, name, value);
	}

	SegmentDistanceIndex::SegmentDistanceIndex(const std::vector<SplineSample>& polyline, const bool closed, const float cellSize)
		: m_samples(polyline)
	{
		const size_t n = m_samples.size();
		if (n == 0)
		{
			return;
		}
		for (const SplineSample& s : m_samples)
		{
			m_bounds.Add(s.x, s.z);
		}
		if (n == 1)
		{
			m_segments.emplace_back(0u, 0u);
		}
		for (size_t i = 0; i + 1 < n; ++i)
		{
			m_segments.emplace_back(static_cast<uint32>(i), static_cast<uint32>(i + 1));
		}
		if (closed && n >= 3)
		{
			m_segments.emplace_back(static_cast<uint32>(n - 1), 0u);
		}

		const double width = static_cast<double>(m_bounds.maxX) - m_bounds.minX;
		const double height = static_cast<double>(m_bounds.maxZ) - m_bounds.minZ;
		double cs = std::isfinite(cellSize) ? std::max(static_cast<double>(cellSize), 0.01) : 1.0;
		cs = std::max({ cs, width / 128.0, height / 128.0 });
		m_cellSize = cs;
		m_originX = m_bounds.minX;
		m_originZ = m_bounds.minZ;
		m_columns = static_cast<int64>(std::floor(width / cs)) + 1;
		m_rows = static_cast<int64>(std::floor(height / cs)) + 1;
		m_cells.resize(static_cast<size_t>(m_columns * m_rows));

		auto cellX = [this](const double x) { return std::clamp<int64>(static_cast<int64>(std::floor((x - m_originX) / m_cellSize)), 0, m_columns - 1); };
		auto cellZ = [this](const double z) { return std::clamp<int64>(static_cast<int64>(std::floor((z - m_originZ) / m_cellSize)), 0, m_rows - 1); };
		for (size_t s = 0; s < m_segments.size(); ++s)
		{
			const SplineSample& a = m_samples[m_segments[s].first];
			const SplineSample& b = m_samples[m_segments[s].second];
			const int64 x0 = cellX(std::min(a.x, b.x));
			const int64 x1 = cellX(std::max(a.x, b.x));
			const int64 z0 = cellZ(std::min(a.z, b.z));
			const int64 z1 = cellZ(std::max(a.z, b.z));
			for (int64 z = z0; z <= z1; ++z)
			{
				for (int64 x = x0; x <= x1; ++x)
				{
					m_cells[static_cast<size_t>(z * m_columns + x)].push_back(static_cast<uint32>(s));
				}
			}
		}
	}

	double SegmentDistanceIndex::SegmentDistance(const uint32 segment, const double x, const double z) const
	{
		const SplineSample& a = m_samples[m_segments[segment].first];
		const SplineSample& b = m_samples[m_segments[segment].second];
		const double dx = static_cast<double>(b.x) - a.x;
		const double dz = static_cast<double>(b.z) - a.z;
		const double len2 = dx * dx + dz * dz;
		double t = 0.0;
		if (len2 > 0.0)
		{
			t = std::clamp(((x - a.x) * dx + (z - a.z) * dz) / len2, 0.0, 1.0);
		}
		return std::hypot(a.x + t * dx - x, a.z + t * dz - z);
	}

	bool SegmentDistanceIndex::Nearest(const float x, const float z, const float maxDistance, float& distance) const
	{
		if (m_segments.empty() || !(maxDistance >= 0.0f) || !std::isfinite(x) || !std::isfinite(z))
		{
			return false;
		}

		// Cell of the query point (may lie outside the grid), clamped so far points cannot overflow.
		constexpr double Far = 1.0e12;
		const int64 cx = static_cast<int64>(std::clamp(std::floor((x - m_originX) / m_cellSize), -Far, Far));
		const int64 cz = static_cast<int64>(std::clamp(std::floor((z - m_originZ) / m_cellSize), -Far, Far));
		const int64 lastX = m_columns - 1;
		const int64 lastZ = m_rows - 1;

		// Rings of cells at Chebyshev distance k; a ring k cell is at least (k - 1) cells away from the point.
		const int64 kMin = std::max<int64>({ 0, -cx, cx - lastX, -cz, cz - lastZ });
		const int64 kMax = std::max({ std::abs(cx), std::abs(cx - lastX), std::abs(cz), std::abs(cz - lastZ) });
		double best = std::numeric_limits<double>::infinity();
		auto visit = [&](const int64 gx, const int64 gz)
		{
			for (const uint32 s : m_cells[static_cast<size_t>(gz * m_columns + gx)])
			{
				best = std::min(best, SegmentDistance(s, x, z));
			}
		};
		for (int64 k = kMin; k <= kMax; ++k)
		{
			const double lower = k > 0 ? static_cast<double>(k - 1) * m_cellSize : 0.0;
			if (lower > maxDistance || lower >= best)
			{
				break;
			}
			if (k == 0)
			{
				visit(cx, cz);
				continue;
			}
			const int64 xa = std::max<int64>(0, cx - k);
			const int64 xb = std::min<int64>(lastX, cx + k);
			for (const int64 gz : { cz - k, cz + k })
			{
				if (gz < 0 || gz > lastZ)
				{
					continue;
				}
				for (int64 gx = xa; gx <= xb; ++gx)
				{
					visit(gx, gz);
				}
			}
			const int64 za = std::max<int64>(0, cz - k + 1);
			const int64 zb = std::min<int64>(lastZ, cz + k - 1);
			for (const int64 gx : { cx - k, cx + k })
			{
				if (gx < 0 || gx > lastX)
				{
					continue;
				}
				for (int64 gz = za; gz <= zb; ++gz)
				{
					visit(gx, gz);
				}
			}
		}
		if (!(best <= maxDistance))
		{
			return false;
		}
		distance = static_cast<float>(best);
		return true;
	}

	const Collection* FindInput(const std::map<String, Collection>& inputs, const char* pin)
	{
		const auto it = inputs.find(pin);
		return it != inputs.end() ? &it->second : nullptr;
	}

	PinDef PointsInputPin()
	{
		return PinDef{ "In", { ItemKind::Points }, true, true };
	}

	bool PrepareControl(NodeOutput& out, const DataItem& item, PreparedControl& prepared)
	{
		const bool area = item.kind == ItemKind::Area;
		if (!area && item.kind != ItemKind::Spline)
		{
			return false;
		}
		if (item.spline.points.size() < (area ? 3u : 1u))
		{
			return false;
		}
		const Bounds2D bounds = SplineBounds(item.spline);
		if (!SaneBounds(bounds))
		{
			WarnOnce(out, "skipped an item with non-finite or out of range geometry");
			return false;
		}
		const SampledPath path(item.spline, EvaluationStep(item.spline, SamplingStep));
		if (area && !path.IsClosed())
		{
			WarnOnce(out, "skipped an area whose spline is not closed");
			return false;
		}
		prepared.item = &item;
		if (area)
		{
			const float rowHeight = std::max(1.0f, (bounds.maxZ - bounds.minZ) / 4096.0f);
			prepared.polygon = std::make_unique<RowIndexedPolygon>(path.GetSamples(), rowHeight);
		}
		prepared.curve = std::make_unique<SegmentDistanceIndex>(path.GetSamples(), path.IsClosed(), ControlDistanceCell);
		return true;
	}

	bool InsideAny(const std::vector<PreparedControl>& areas, const float x, const float z)
	{
		for (const PreparedControl& area : areas)
		{
			if (area.polygon->Contains(x, z))
			{
				return true;
			}
		}
		return false;
	}

	float ItemClearance(NodeOutput& out, const DataItem& item, const String& attribute)
	{
		if (attribute.empty())
		{
			return 0.0f;
		}
		const auto it = item.attributes.find(attribute);
		if (it == item.attributes.end())
		{
			return 0.0f;
		}
		float value = 0.0f;
		if (std::holds_alternative<bool>(it->second) || !ToNumber(it->second, value))
		{
			WarnOnce(out, "clearance attribute '" + attribute + "' is not a finite number on an item, 0 used");
			return 0.0f;
		}
		return value;
	}
}
