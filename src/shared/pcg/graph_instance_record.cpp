// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "graph_instance_record.h"
#include "record_io.h"
#include "graph/json_util.h"
#include "graph/node_util.h"

#include <algorithm>
#include <cmath>

namespace mmo::pcg
{
	using namespace record_io;

	namespace
	{
		using graph::jsonutil::Json;

		/// PCGG as little endian characters.
		constexpr uint32 MagicPcgg = 0x47474350;
		/// 1: initial layout. Placements use the oasis record's version 4 layout.
		/// 2: flow paths after the owned content.
		constexpr uint32 FormatVersion = 2;
		/// Oldest version that still reads.
		constexpr uint32 OldestVersion = 1;
		/// Bytes per flow point: x, z, halfWidth.
		constexpr size_t FlowPointBytes = 3 * sizeof(float);
		/// Placement layout version shared with the oasis record.
		constexpr uint32 PlacementVersion = 4;
		/// How far probe points are nudged off an edge into the polygon.
		constexpr double ProbeOffset = 1.0e-3;
		/// Distance from a line within which a point counts as on it; well below ProbeOffset.
		constexpr double CollinearTolerance = 1.0e-4;

		/// Which side of the line a-b the point c lies on: 1, -1, or 0 when it is within CollinearTolerance of the line
		/// (sampled points on a shared slanted edge differ by float rounding, which must not count as a crossing).
		int Orientation(const double ax, const double az, const double bx, const double bz, const double cx, const double cz)
		{
			const double len = std::hypot(bx - ax, bz - az);
			if (len <= 0.0)
			{
				return 0;
			}
			const double distance = ((bx - ax) * (cz - az) - (bz - az) * (cx - ax)) / len;
			return distance > CollinearTolerance ? 1 : (distance < -CollinearTolerance ? -1 : 0);
		}

		/// Whether two segments cross at a point interior to both (touching and collinear contact do not count).
		bool ProperlyCross(const SplineSample& p1, const SplineSample& p2, const SplineSample& q1, const SplineSample& q2)
		{
			const int o1 = Orientation(p1.x, p1.z, p2.x, p2.z, q1.x, q1.z);
			const int o2 = Orientation(p1.x, p1.z, p2.x, p2.z, q2.x, q2.z);
			const int o3 = Orientation(q1.x, q1.z, q2.x, q2.z, p1.x, p1.z);
			const int o4 = Orientation(q1.x, q1.z, q2.x, q2.z, p2.x, p2.z);
			return o1 * o2 < 0 && o3 * o4 < 0;
		}

		/// Whether any edge of one polygon properly crosses an edge of the other.
		bool AnyEdgesCross(const std::vector<SplineSample>& a, const std::vector<SplineSample>& b)
		{
			const size_t na = a.size();
			const size_t nb = b.size();
			for (size_t i = 0; i < na; ++i)
			{
				const SplineSample& p1 = a[i];
				const SplineSample& p2 = a[(i + 1) % na];
				const float minX = std::min(p1.x, p2.x);
				const float maxX = std::max(p1.x, p2.x);
				const float minZ = std::min(p1.z, p2.z);
				const float maxZ = std::max(p1.z, p2.z);
				for (size_t j = 0; j < nb; ++j)
				{
					const SplineSample& q1 = b[j];
					const SplineSample& q2 = b[(j + 1) % nb];
					if (std::max(q1.x, q2.x) < minX || std::min(q1.x, q2.x) > maxX ||
						std::max(q1.z, q2.z) < minZ || std::min(q1.z, q2.z) > maxZ)
					{
						continue;
					}
					if (ProperlyCross(p1, p2, q1, q2))
					{
						return true;
					}
				}
			}
			return false;
		}

		/// Whether a point just inside an edge of a is inside both a and b. Without crossing edges this finds
		/// containment and edge-sharing overlap; contact from outside never has such a point.
		/// Complete because with no proper crossings, shared interior implies a boundary edge of one runs through the
		/// interior of the other, or along its edge with the interior on the same side; either way its midpoint probe hits.
		bool AnyProbeInsideBoth(const SampledPath& a, const graph::RowIndexedPolygon& bPolygon)
		{
			const std::vector<SplineSample>& s = a.GetSamples();
			const size_t n = s.size();
			double area2 = 0.0;
			for (size_t i = 0; i < n; ++i)
			{
				const SplineSample& p = s[i];
				const SplineSample& q = s[(i + 1) % n];
				area2 += static_cast<double>(p.x) * q.z - static_cast<double>(q.x) * p.z;
			}
			// Left of an edge is inside for a counter clockwise polygon.
			const double side = area2 >= 0.0 ? 1.0 : -1.0;

			for (size_t i = 0; i < n; ++i)
			{
				const SplineSample& p = s[i];
				const SplineSample& q = s[(i + 1) % n];
				const double dx = static_cast<double>(q.x) - p.x;
				const double dz = static_cast<double>(q.z) - p.z;
				const double len = std::hypot(dx, dz);
				if (len <= 0.0)
				{
					continue;
				}
				const double mx = (static_cast<double>(p.x) + q.x) * 0.5 - dz / len * ProbeOffset * side;
				const double mz = (static_cast<double>(p.z) + q.z) * 0.5 + dx / len * ProbeOffset * side;
				const float fx = static_cast<float>(mx);
				const float fz = static_cast<float>(mz);
				if (a.Contains(fx, fz) && bPolygon.Contains(fx, fz))
				{
					return true;
				}
			}
			return false;
		}

		Json BuildBlob(const GraphInstanceRecord& record)
		{
			Json blob = Json::object();
			blob["name"] = record.name;
			blob["graphPath"] = record.graphPath;
			blob["seed"] = record.seed;
			blob["pointLimit"] = static_cast<uint64>(record.pointLimit);
			blob["borderFade"] = record.borderFade;
			if (record.seamHash != 0)
			{
				blob["seamHash"] = record.seamHash;
			}
			Json overrides = Json::object();
			for (const auto& entry : record.parameterOverrides)
			{
				overrides[entry.first] = graph::jsonutil::ValueToJson(entry.second);
			}
			blob["parameterOverrides"] = overrides;
			return blob;
		}

		bool ReadUInt64(const Json& obj, const char* key, uint64& out)
		{
			const Json* v = graph::jsonutil::Find(obj, key);
			if (v == nullptr || !v->is_number_unsigned())
			{
				return false;
			}
			out = v->get<uint64_t>();
			return true;
		}

		bool ParseBlob(const String& text, GraphInstanceRecord& out)
		{
			const Json blob = Json::parse(text, nullptr, false);
			if (!blob.is_object())
			{
				return false;
			}

			String error;
			uint64 limit = 0;
			if (!graph::jsonutil::ReadString(blob, "name", out.name, "instance", error, true) ||
				!graph::jsonutil::ReadString(blob, "graphPath", out.graphPath, "instance", error, true) ||
				!ReadUInt64(blob, "seed", out.seed) || !ReadUInt64(blob, "pointLimit", limit))
			{
				return false;
			}
			out.pointLimit = static_cast<size_t>(limit);

			// Optional: records written before it existed keep the default.
			out.borderFade = GraphInstanceRecord().borderFade;
			if (!graph::jsonutil::ReadNumber(blob, "borderFade", out.borderFade, "instance", error) || out.borderFade < 0.0f)
			{
				return false;
			}

			// Optional: a missing key (a build without seams) is 0.
			out.seamHash = 0;
			if (graph::jsonutil::Find(blob, "seamHash") != nullptr && !ReadUInt64(blob, "seamHash", out.seamHash))
			{
				return false;
			}

			const Json* overrides = graph::jsonutil::Find(blob, "parameterOverrides");
			if (overrides == nullptr || !overrides->is_object())
			{
				return false;
			}
			for (auto it = overrides->begin(); it != overrides->end(); ++it)
			{
				graph::AttributeValue value;
				if (!graph::jsonutil::ValueFromJson(it.value(), value, error))
				{
					return false;
				}
				out.parameterOverrides[it.key()] = value;
			}
			return true;
		}
	}

	bool WriteGraphInstanceRecord(io::Writer& writer, const GraphInstanceRecord& record)
	{
		if (record.pointIds.size() != record.area.points.size())
		{
			return false;
		}

		Put(writer, MagicPcgg);
		Put(writer, FormatVersion);
		Put(writer, record.id);
		PutString(writer, graph::jsonutil::Dump(BuildBlob(record)));

		Put<uint32>(writer, static_cast<uint32>(record.area.points.size()));
		for (size_t i = 0; i < record.area.points.size(); ++i)
		{
			PutSplinePoint(writer, record.area.points[i]);
			Put(writer, record.pointIds[i]);
		}
		Put(writer, record.nextPointId);
		PutOwnedContent(writer, record.content);

		Put<uint32>(writer, static_cast<uint32>(record.flowPaths.size()));
		for (const terrain_io::water_flow::FlowPath& path : record.flowPaths)
		{
			Put<uint32>(writer, static_cast<uint32>(path.size()));
			for (const terrain_io::water_flow::FlowPoint& point : path)
			{
				Put(writer, point.x);
				Put(writer, point.z);
				Put(writer, point.halfWidth);
			}
		}
		return true;
	}

	bool ReadGraphInstanceRecord(io::Reader& reader, GraphInstanceRecord& out)
	{
		uint32 magic = 0;
		uint32 version = 0;
		if (!Get(reader, magic) || magic != MagicPcgg || !Get(reader, version) || version < OldestVersion || version > FormatVersion)
		{
			return false;
		}

		out = GraphInstanceRecord();
		String blob;
		if (!Get(reader, out.id) || !GetString(reader, blob) || !ParseBlob(blob, out))
		{
			return false;
		}

		out.area.closed = true;
		out.area.interpolation = SplineInterpolation::CatmullRom;
		uint32 pointCount = 0;
		if (!GetCount(reader, pointCount, SplinePointBytes + 8))
		{
			return false;
		}
		out.area.points.resize(pointCount);
		out.pointIds.resize(pointCount);
		for (uint32 i = 0; i < pointCount; ++i)
		{
			if (!GetSplinePoint(reader, out.area.points[i]) || !Get(reader, out.pointIds[i]))
			{
				return false;
			}
		}
		if (!Get(reader, out.nextPointId))
		{
			return false;
		}
		for (const uint64 id : out.pointIds)
		{
			if (id >= out.nextPointId)
			{
				out.nextPointId = id + 1;
			}
		}

		if (!GetOwnedContent(reader, out.content, PlacementVersion))
		{
			return false;
		}
		if (version < 2)
		{
			return true;
		}

		uint32 pathCount = 0;
		if (!GetCount(reader, pathCount, sizeof(uint32)))
		{
			return false;
		}
		out.flowPaths.resize(pathCount);
		for (terrain_io::water_flow::FlowPath& path : out.flowPaths)
		{
			uint32 pointCount = 0;
			if (!GetCount(reader, pointCount, FlowPointBytes))
			{
				return false;
			}
			path.resize(pointCount);
			for (terrain_io::water_flow::FlowPoint& point : path)
			{
				if (!Get(reader, point.x) || !Get(reader, point.z) || !Get(reader, point.halfWidth))
				{
					return false;
				}
			}
		}
		return true;
	}

	bool AreasOverlap(const PathSpline& a, const PathSpline& b)
	{
		if (a.points.size() < 3 || b.points.size() < 3)
		{
			return false;
		}

		// Translate both areas by a common origin so the probe math below runs near zero: far from the origin the
		// float spacing would swallow the small inward nudge.
		float originX = a.points[0].x;
		float originZ = a.points[0].z;
		for (const PathSpline* spline : { &a, &b })
		{
			for (const SplinePoint& p : spline->points)
			{
				originX = std::min(originX, p.x);
				originZ = std::min(originZ, p.z);
			}
		}
		PathSpline closedA = a;
		closedA.closed = true;
		PathSpline closedB = b;
		closedB.closed = true;
		for (PathSpline* spline : { &closedA, &closedB })
		{
			for (SplinePoint& p : spline->points)
			{
				p.x -= originX;
				p.z -= originZ;
			}
		}
		const SampledPath pathA(closedA, graph::EvaluationStep(closedA, graph::SamplingStep));
		const SampledPath pathB(closedB, graph::EvaluationStep(closedB, graph::SamplingStep));
		const std::vector<SplineSample>& sa = pathA.GetSamples();
		const std::vector<SplineSample>& sb = pathB.GetSamples();
		if (sa.size() < 3 || sb.size() < 3)
		{
			return false;
		}

		float aMinX = 0.0f, aMinZ = 0.0f, aMaxX = 0.0f, aMaxZ = 0.0f;
		float bMinX = 0.0f, bMinZ = 0.0f, bMaxX = 0.0f, bMaxZ = 0.0f;
		pathA.GetBounds(aMinX, aMinZ, aMaxX, aMaxZ);
		pathB.GetBounds(bMinX, bMinZ, bMaxX, bMaxZ);
		if (aMaxX <= bMinX || bMaxX <= aMinX || aMaxZ <= bMinZ || bMaxZ <= aMinZ)
		{
			return false;
		}

		if (AnyEdgesCross(sa, sb))
		{
			return true;
		}

		const graph::RowIndexedPolygon polyA(sa, std::max(1.0f, (aMaxZ - aMinZ) / 4096.0f));
		const graph::RowIndexedPolygon polyB(sb, std::max(1.0f, (bMaxZ - bMinZ) / 4096.0f));
		return AnyProbeInsideBoth(pathA, polyB) || AnyProbeInsideBoth(pathB, polyA);
	}
}
