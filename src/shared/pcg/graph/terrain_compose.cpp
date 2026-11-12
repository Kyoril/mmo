// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "terrain_compose.h"

#include "node_util.h"
#include "polyline_index.h"

#include "pcg/pcg_random.h"
#include "pcg/section_path.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <utility>
#include <variant>

namespace mmo::pcg::graph
{
	namespace
	{
		/// Seed of the Noise op's fractal noise (combined with the op order).
		constexpr uint64 kNoiseSeed = 0x7e44a1f3c0de5eedull;

		float Saturate(const float v)
		{
			return std::min(1.0f, std::max(0.0f, v));
		}

		float Smoothstep01(float t)
		{
			t = Saturate(t);
			return t * t * (3.0f - 2.0f * t);
		}

		bool Contains(const WorldRect& r, const float x, const float z)
		{
			return x >= r.minX && x <= r.maxX && z >= r.minZ && z <= r.maxZ;
		}

		/// A region edge segment lies on a neighbour edge if points along it are within this (m) of the neighbour's border.
		constexpr float kSeamTolerance = 0.05f;

		/// Whether the region segment a-b lies on the neighbour's border: both end points and points along it (eighths)
		/// all lie within the seam tolerance of the neighbour loop (indexed with that reach). A segment that only ends at a
		/// neighbour corner, or cuts across it, fails.
		bool IsSeamSegment(const SplineSample& a, const SplineSample& b, const PolylineIndex& neighbour)
		{
			NearestHit hit;
			for (int step = 0; step <= 8; ++step)
			{
				const double t = static_cast<double>(step) / 8.0;
				const double x = a.x + (static_cast<double>(b.x) - a.x) * t;
				const double z = a.z + (static_cast<double>(b.z) - a.z) * t;
				if (!neighbour.Nearest(static_cast<float>(x), static_cast<float>(z), hit))
				{
					return false;
				}
			}
			return true;
		}

		/// FNV-1a step.
		uint64 HashStep(uint64 h, const int64 value)
		{
			h ^= static_cast<uint64>(value);
			h *= 0x100000001b3ull;
			return h;
		}

		WorldRect Grow(WorldRect r, const float by)
		{
			r.minX -= by;
			r.minZ -= by;
			r.maxX += by;
			r.maxZ += by;
			return r;
		}

		WorldRect PathRect(const SampledPath& path)
		{
			WorldRect r;
			path.GetBounds(r.minX, r.minZ, r.maxX, r.maxZ);
			return r;
		}

		/// Seed param of a Noise op: Int as is, Float rounded to the nearest integer; op.order if absent or unusable.
		int64 NoiseSeedParam(const TerrainOp& op)
		{
			const auto it = op.params.find("seed");
			if (it != op.params.end())
			{
				if (const int64* i = std::get_if<int64>(&it->second))
				{
					return *i;
				}
				if (const float* f = std::get_if<float>(&it->second))
				{
					if (std::isfinite(*f) && std::abs(*f) < 9.0e18f)
					{
						return static_cast<int64>(std::llround(static_cast<double>(*f)));
					}
				}
			}
			return static_cast<int64>(op.order);
		}

		/// Containment index of a closed path, rows as PrepareControl builds them.
		std::unique_ptr<RowIndexedPolygon> MakePolygon(const SampledPath& path)
		{
			const WorldRect r = PathRect(path);
			return std::make_unique<RowIndexedPolygon>(path.GetSamples(), std::max(1.0f, (r.maxZ - r.minZ) / 4096.0f));
		}

		String NodeLabel(const TerrainOpInput& in)
		{
			return String(TerrainOpTypeName(in.op.type)) + " op of node " + std::to_string(in.op.order);
		}

		/// Distance between rim samples of a basin.
		constexpr float kRimStep = 2.0f;

		/// Preferred distance between the water surface samples of a river (doubled for very long rivers).
		constexpr float kRiverSurfaceStep = 1.0f;

		/// Most surface samples per river; the step doubles until the river fits.
		constexpr float kMaxRiverSurfaceSamples = 100000.0f;

		/// Flow path points are every this many surface samples (4 m at the preferred step).
		constexpr size_t kRiverFlowStride = 4;

		/// Whether (x, z), whose nearest point on the open polyline `samples` is `hit`, projects past an end of it (before
		/// the first sample or after the last, along the end segment). Then `past` is the distance beyond the end line and
		/// `lateral` the distance from the end segment's line.
		bool RiverPastEnd(const std::vector<SplineSample>& samples, const NearestHit& hit, const float x, const float z,
			float& past, float& lateral, bool& atStartOut)
		{
			if (samples.size() < 2)
			{
				return false;
			}
			const uint32 last = static_cast<uint32>(samples.size() - 1);
			const bool atStart = hit.a == 0 && hit.b == 1 && hit.t <= 0.0f;
			const bool atEnd = hit.a + 1 == last && hit.b == last && hit.t >= 1.0f;
			if (!atStart && !atEnd)
			{
				return false;
			}
			const SplineSample& end = atStart ? samples[0] : samples[last];
			const SplineSample& inner = atStart ? samples[1] : samples[last - 1];
			const float dx = end.x - inner.x;
			const float dz = end.z - inner.z;
			const float length = std::hypot(dx, dz);
			if (!(length > 0.0f))
			{
				return false;
			}
			// Outward along the end segment.
			const float ux = dx / length;
			const float uz = dz / length;
			const float ox = x - end.x;
			const float oz = z - end.z;
			const float beyond = ox * ux + oz * uz;
			if (!(beyond > 0.0f))
			{
				return false;
			}
			past = beyond;
			lateral = std::abs(oz * ux - ox * uz);
			atStartOut = atStart;
			return true;
		}

		/// A river's bank tops out at least this far above its water surface, so the water's visible edge is where the
		/// bank crosses the surface and not the edge of the watered quads.
		constexpr float kRiverFreeboard = 0.25f;

		/// A river spline may climb this far above its running water surface without a warning.
		constexpr float kRiverClimbTolerance = 0.5f;

		/// A length in metres as short text: at most 2 decimals, no trailing zeros ("20", "21.5").
		String FormatMeters(const float value)
		{
			char buffer[48];
			std::snprintf(buffer, sizeof(buffer), "%.2f", static_cast<double>(value));
			String text = buffer;
			if (text.find('.') != String::npos)
			{
				while (text.back() == '0')
				{
					text.pop_back();
				}
				if (text.back() == '.')
				{
					text.pop_back();
				}
			}
			return text == "-0" ? String("0") : text;
		}

		/// Centroid of a closed polyline (area-weighted; the vertex average if it has no area).
		Vector3 AreaCentroid(const std::vector<SplineSample>& samples)
		{
			double area = 0.0;
			double cx = 0.0;
			double cz = 0.0;
			double ax = 0.0;
			double az = 0.0;
			const size_t count = samples.size();
			for (size_t i = 0; i < count; ++i)
			{
				const SplineSample& p = samples[i];
				const SplineSample& q = samples[(i + 1) % count];
				const double cross = static_cast<double>(p.x) * q.z - static_cast<double>(q.x) * p.z;
				area += cross;
				cx += (static_cast<double>(p.x) + q.x) * cross;
				cz += (static_cast<double>(p.z) + q.z) * cross;
				ax += p.x;
				az += p.z;
			}
			if (std::abs(area) > 1e-9)
			{
				return Vector3(static_cast<float>(cx / (3.0 * area)), 0.0f, static_cast<float>(cz / (3.0 * area)));
			}
			return count > 0 ? Vector3(static_cast<float>(ax / count), 0.0f, static_cast<float>(az / count)) : Vector3::Zero;
		}

		/// Lowest ground under a closed polyline, sampled at its vertices and every kRimStep along its segments.
		float LowestRim(const std::vector<SplineSample>& samples, const std::function<float(float, float)>& ground)
		{
			float lowest = std::numeric_limits<float>::max();
			const size_t count = samples.size();
			for (size_t i = 0; i < count; ++i)
			{
				const SplineSample& p = samples[i];
				const SplineSample& q = samples[(i + 1) % count];
				const float length = std::hypot(q.x - p.x, q.z - p.z);
				const int pieces = static_cast<int>(std::min(100000.0f, std::max(1.0f, std::ceil(length / kRimStep))));
				for (int k = 0; k < pieces; ++k)
				{
					const float t = static_cast<float>(k) / static_cast<float>(pieces);
					lowest = std::min(lowest, ground(p.x + (q.x - p.x) * t, p.z + (q.z - p.z) * t));
				}
			}
			return lowest;
		}

		/// A region that only touches a tile's border does not claim the tile: tile rectangles are shrunk by this (m).
		constexpr float kTileBorderTolerance = 1e-3f;

		/// Whether the segment a-b has a point in the closed rect (Liang-Barsky clipping).
		bool SegmentHitsRect(const SplineSample& a, const SplineSample& b, const WorldRect& r)
		{
			const double dx = static_cast<double>(b.x) - a.x;
			const double dz = static_cast<double>(b.z) - a.z;
			const std::array<double, 4> p{ -dx, dx, -dz, dz };
			const std::array<double, 4> q{ static_cast<double>(a.x) - r.minX, static_cast<double>(r.maxX) - a.x,
				static_cast<double>(a.z) - r.minZ, static_cast<double>(r.maxZ) - a.z };
			double t0 = 0.0;
			double t1 = 1.0;
			for (size_t i = 0; i < 4; ++i)
			{
				if (p[i] == 0.0)
				{
					if (q[i] < 0.0)
					{
						return false;
					}
					continue;
				}
				const double t = q[i] / p[i];
				if (p[i] < 0.0)
				{
					t0 = std::max(t0, t);
				}
				else
				{
					t1 = std::min(t1, t);
				}
				if (t0 > t1)
				{
					return false;
				}
			}
			return true;
		}

		double PointRectDistance(const double x, const double z, const WorldRect& r)
		{
			const double dx = std::max({ static_cast<double>(r.minX) - x, 0.0, x - r.maxX });
			const double dz = std::max({ static_cast<double>(r.minZ) - z, 0.0, z - r.maxZ });
			return std::hypot(dx, dz);
		}

		double PointSegmentDistance(const double x, const double z, const SplineSample& a, const SplineSample& b)
		{
			const double dx = static_cast<double>(b.x) - a.x;
			const double dz = static_cast<double>(b.z) - a.z;
			const double lengthSq = dx * dx + dz * dz;
			const double t = lengthSq > 0.0 ? std::min(1.0, std::max(0.0, ((x - a.x) * dx + (z - a.z) * dz) / lengthSq)) : 0.0;
			return std::hypot(x - (a.x + dx * t), z - (a.z + dz * t));
		}

		/// Distance between the segment a-b and the closed rect (0 if they meet).
		double SegmentRectDistance(const SplineSample& a, const SplineSample& b, const WorldRect& r)
		{
			if (SegmentHitsRect(a, b, r))
			{
				return 0.0;
			}
			double d = std::min(PointRectDistance(a.x, a.z, r), PointRectDistance(b.x, b.z, r));
			d = std::min(d, PointSegmentDistance(r.minX, r.minZ, a, b));
			d = std::min(d, PointSegmentDistance(r.maxX, r.minZ, a, b));
			d = std::min(d, PointSegmentDistance(r.minX, r.maxZ, a, b));
			d = std::min(d, PointSegmentDistance(r.maxX, r.maxZ, a, b));
			return d;
		}

		/// Whether the inside of the closed loop `samples` (contained by `polygon`) meets the rect: the rect's centre is
		/// inside, or an edge of the loop crosses the rect (which includes a vertex lying in it).
		bool LoopTouchesRect(const std::vector<SplineSample>& samples, const RowIndexedPolygon& polygon, const WorldRect& r)
		{
			if (polygon.Contains((r.minX + r.maxX) * 0.5f, (r.minZ + r.maxZ) * 0.5f))
			{
				return true;
			}
			const size_t count = samples.size();
			for (size_t i = 0; i < count; ++i)
			{
				if (SegmentHitsRect(samples[i], samples[(i + 1) % count], r))
				{
					return true;
				}
			}
			return false;
		}

		/// Whether the open polyline `samples`, grown by `width`, meets the rect.
		bool LineTouchesRect(const std::vector<SplineSample>& samples, const float width, const WorldRect& r)
		{
			if (samples.size() == 1)
			{
				return PointRectDistance(samples[0].x, samples[0].z, r) < width;
			}
			for (size_t i = 0; i + 1 < samples.size(); ++i)
			{
				if (SegmentRectDistance(samples[i], samples[i + 1], r) < width)
				{
					return true;
				}
			}
			return false;
		}

		/// Adds the layer weights `splat` at `weight` over the composited paint (premultiplied, Porter-Duff over).
		void CompositeSplat(std::array<double, 4>& premultiplied, double& alpha, const std::array<uint8, 4>& splat, const float weight)
		{
			const double w = std::min(1.0, std::max(0.0, static_cast<double>(weight)));
			if (w <= 0.0)
			{
				return;
			}
			alpha = alpha * (1.0 - w) + w;
			for (size_t i = 0; i < 4; ++i)
			{
				premultiplied[i] = premultiplied[i] * (1.0 - w) + static_cast<double>(splat[i]) * w;
			}
		}
	}

	struct PreparedTerrain::PreparedOp
	{
		TerrainOpType type = TerrainOpType::Noise;
		uint32 order = 0;
		uint64 sourceId = 0;
		/// Bounds of every position the op may change.
		WorldRect bounds;
		/// Area ops (and the region): even-odd containment.
		std::unique_ptr<RowIndexedPolygon> polygon;
		/// Distance to the spline, or to the area edge, within the op's reach.
		PolylineIndex edge;

		// Noise.
		float amplitude = 0.0f;
		float frequency = 0.0f;
		uint32 octaves = 1;
		uint64 noiseSeed = 0;

		// Noise, Paint (areas), region: inward fade distance. Raise, Carve, Paint (splines): width per side; Carve: bank.
		float falloff = 0.0f;
		float width = 0.0f;
		float falloffPower = 1.0f;
		float depth = 0.0f;
		float bank = 0.0f;
		/// Raise: peak height per spline sample.
		std::vector<float> peaks;

		// Paint.
		String textureKey;
		float weight = 1.0f;
		bool area = false;
		/// Material the painted tiles are switched to; empty = none.
		String tileMaterial;

		// WaterArea.
		float level = 0.0f;
		float margin = 0.0f;
		/// `depth` > 0 makes the area a basin; `basinEdge` finds the inward distance within shoreWidth.
		float shoreWidth = 0.0f;
		PolylineIndex basinEdge;
		Vector3 centroid = Vector3::Zero;

		// River (also width, bank, depth; margin = waterMargin).
		/// Water surface samples downstream (distance = arc distance, y = running minimum surface), surfaceStep apart
		/// except for the last one, which sits at the end of the spline.
		std::vector<SplineSample> surface;
		float surfaceStep = kRiverSurfaceStep;
		/// Bed edges at +-width (left of the flow, then right), one point per surface sample, y = bed height.
		std::array<std::vector<Vector3>, 2> bedEdges;

		/// River: distance from the spline that still holds water (the bank's top plus waterMargin).
		float WaterReach() const
		{
			return width + bank + margin;
		}

		/// River: water surface at an arc distance, interpolated between the surface samples (clamped to the ends).
		float SurfaceAt(const float distance) const
		{
			if (surface.empty())
			{
				return 0.0f;
			}
			if (surface.size() == 1 || !(distance > 0.0f))
			{
				return surface.front().y;
			}
			const size_t i = std::min(surface.size() - 2, static_cast<size_t>(std::max(0.0f, std::floor(distance / surfaceStep))));
			const SplineSample& a = surface[i];
			const SplineSample& b = surface[i + 1];
			const float span = b.distance - a.distance;
			const float t = span > 0.0f ? Saturate((distance - a.distance) / span) : 0.0f;
			return a.y + (b.y - a.y) * t;
		}

		// SectionPath.
		SectionPathShape shape;
		bool remapLayers = false;
		/// Section asset path (for warnings).
		String sectionAsset;
		/// Section layers the section paints somewhere (variant paint with coverage, or the mass paint layer).
		std::array<bool, 4> paintedLayers{};
		/// Exclusion shapes as the items PrepareControl prepared (the prepared controls point into them).
		std::vector<std::unique_ptr<DataItem>> exclusionItems;
		/// Exclusion areas and splines, prepared as Difference prepares its Subtract input.
		std::vector<PreparedControl> exclusionAreas;
		std::vector<SplineTarget> exclusionSplines;

		/// SectionPath: whether a prop at (x, z) is cut by an exclusion (the same tests as Difference).
		bool Excluded(const float x, const float z) const
		{
			if (InsideAny(exclusionAreas, x, z))
			{
				return true;
			}
			for (const SplineTarget& spline : exclusionSplines)
			{
				float d = 0.0f;
				if (spline.control.curve->Nearest(x, z, spline.radius, d))
				{
					return true;
				}
			}
			return false;
		}
	};

	std::vector<TerrainOpInput> CollectTerrainOps(const Graph& graph, const EvalResult& result)
	{
		std::set<uint32> nodeIds;
		for (const NodeInstance& node : graph.nodes)
		{
			nodeIds.insert(node.id);
		}

		std::vector<std::tuple<int32, uint32, uint32, size_t, TerrainOpInput>> gathered;
		for (const auto& [id, nodeResult] : result.nodes)
		{
			if (nodeIds.count(id) == 0)
			{
				continue;
			}
			const auto pin = nodeResult.output.pins.find("Terrain");
			if (pin == nodeResult.output.pins.end())
			{
				continue;
			}
			for (size_t i = 0; i < pin->second.size(); ++i)
			{
				const DataItem& item = pin->second[i];
				if (item.kind != ItemKind::Terrain)
				{
					continue;
				}
				TerrainOpInput in;
				in.op = item.op;
				in.spline = item.spline;
				in.pointIds = item.pointIds;
				in.points = item.points;
				in.sourceId = item.sourceId;
				in.itemIndex = static_cast<uint32>(i);
				in.exclusions = item.exclusions;
				gathered.emplace_back(item.op.stackOrder, item.op.order, id, i, std::move(in));
			}
		}

		std::stable_sort(gathered.begin(), gathered.end(), [](const auto& a, const auto& b)
		{
			return std::tie(std::get<0>(a), std::get<1>(a), std::get<2>(a), std::get<3>(a)) < std::tie(std::get<0>(b), std::get<1>(b), std::get<2>(b), std::get<3>(b));
		});

		std::vector<TerrainOpInput> out;
		out.reserve(gathered.size());
		for (auto& g : gathered)
		{
			out.push_back(std::move(std::get<4>(g)));
		}
		return out;
	}

	PreparedTerrain::PreparedTerrain() = default;
	PreparedTerrain::~PreparedTerrain() = default;
	PreparedTerrain::PreparedTerrain(PreparedTerrain&&) noexcept = default;
	PreparedTerrain& PreparedTerrain::operator=(PreparedTerrain&&) noexcept = default;

	bool PreparedTerrain::Prepare(const std::vector<TerrainOpInput>& ops, const std::function<float(float, float)>& baseGround,
		const SectionAssetProvider& sections, const PathSpline& region, const float borderFade, String& error,
		const std::vector<PathSpline>& neighbours)
	{
		m_ops.clear();
		m_region.reset();
		m_influence = WorldRect{};
		m_waterMaterial.clear();
		m_waterMaterialFromRiver = false;
		m_warnings.clear();
		m_borderFade = std::isfinite(borderFade) ? std::max(0.0f, borderFade) : 0.0f;
		m_seamHash = 0;
		m_seamNeighbours.clear();
		m_tileLayout = GridLayout();
		m_tileMaterials.clear();
		m_tileMaterialConflicts.clear();
		error.clear();

		auto ground = [&baseGround](const float x, const float z)
		{
			return baseGround ? baseGround(x, z) : 0.0f;
		};
		auto fail = [this, &error](const String& message)
		{
			m_ops.clear();
			m_region.reset();
			m_influence = WorldRect{};
			m_waterMaterial.clear();
			m_waterMaterialFromRiver = false;
			m_seamHash = 0;
			m_seamNeighbours.clear();
			error = message;
			return false;
		};

		// Region.
		{
			const Bounds2D bounds = SplineBounds(region);
			if (region.points.size() < 3 || !region.closed || !SaneBounds(bounds))
			{
				return fail("The region is not a closed loop of at least 3 points with sane coordinates.");
			}
			const SampledPath path(region, EvaluationStep(region, SamplingStep));
			auto prepared = std::make_unique<PreparedOp>();
			prepared->polygon = MakePolygon(path);

			// Seams: the segments that lie on a neighbour's border do not fade (the neighbour fades to nothing there
			// as well, which would leave a dip between the two).
			const std::vector<SplineSample>& samples = path.GetSamples();
			const size_t sampleCount = samples.size();
			std::vector<uint8> seamSegments(sampleCount, 0);
			std::vector<uint8> touching(neighbours.size(), 0);
			uint64 seamHash = 0xcbf29ce484222325ull;
			bool anySeam = false;
			for (size_t n = 0; n < neighbours.size() && sampleCount >= 3; ++n)
			{
				const PathSpline& other = neighbours[n];
				const Bounds2D otherBounds = SplineBounds(other);
				if (other.points.size() < 3 || !other.closed || !SaneBounds(otherBounds))
				{
					continue;
				}

				constexpr float reach = 1.0f;
				if (otherBounds.maxX + reach < bounds.minX || otherBounds.minX - reach > bounds.maxX ||
					otherBounds.maxZ + reach < bounds.minZ || otherBounds.minZ - reach > bounds.maxZ)
				{
					continue;
				}

				const SampledPath otherPath(other, EvaluationStep(other, SamplingStep));
				const std::vector<SplineSample>& otherSamples = otherPath.GetSamples();
				if (otherSamples.size() < 3)
				{
					continue;
				}
				// Indexed: a linear scan per query is O(region samples x neighbour samples).
				PolylineIndex otherIndex;
				otherIndex.Build(otherSamples, true, kSeamTolerance);
				for (size_t i = 0; i < sampleCount; ++i)
				{
					const SplineSample& a = samples[i];
					const SplineSample& b = samples[(i + 1) % sampleCount];
					if (IsSeamSegment(a, b, otherIndex))
					{
						touching[n] = 1;
						if (!seamSegments[i])
						{
							seamSegments[i] = 1;
							anySeam = true;
							seamHash = HashStep(seamHash, static_cast<int64>(std::llround(a.x * 100.0f)));
							seamHash = HashStep(seamHash, static_cast<int64>(std::llround(a.z * 100.0f)));
							seamHash = HashStep(seamHash, static_cast<int64>(std::llround(b.x * 100.0f)));
							seamHash = HashStep(seamHash, static_cast<int64>(std::llround(b.z * 100.0f)));
						}
					}
				}
			}
			if (anySeam)
			{
				// Never 0: 0 means no seams.
				m_seamHash = seamHash | 1ull;
				for (size_t n = 0; n < touching.size(); ++n)
				{
					if (touching[n])
					{
						m_seamNeighbours.push_back(n);
					}
				}
			}

			prepared->edge.Build(samples, true, m_borderFade, seamSegments);
			prepared->bounds = PathRect(path);
			m_region = std::move(prepared);
		}

		for (const TerrainOpInput& in : ops)
		{
			const TerrainOp& op = in.op;
			const String label = NodeLabel(in);
			auto prepared = std::make_unique<PreparedOp>();
			prepared->type = op.type;
			prepared->order = op.order;
			prepared->sourceId = in.sourceId;

			const bool needsArea = op.type == TerrainOpType::Noise || op.type == TerrainOpType::WaterArea;
			const bool needsOpen = op.type == TerrainOpType::River;
			const size_t minPoints = needsArea || (op.type == TerrainOpType::Paint && in.spline.closed) ? 3u
				: (op.type == TerrainOpType::SectionPath || needsOpen ? 2u : 1u);
			const bool saneGeometry = in.spline.points.size() >= minPoints && SaneBounds(SplineBounds(in.spline));
			if (op.type == TerrainOpType::SectionPath)
			{
				if (!saneGeometry)
				{
					return fail(label + ": section path '" + op.asset + "' needs at least 2 points with sane coordinates.");
				}
			}
			else if (!saneGeometry || (needsArea && !in.spline.closed) || (needsOpen && in.spline.closed))
			{
				Warn(label + ": skipped, its geometry is not " + (needsArea ? "a closed area" : (needsOpen ? "an open spline" : "a usable spline")) + ".");
				continue;
			}

			switch (op.type)
			{
			case TerrainOpType::SectionPath:
			{
				String assetError;
				const SectionAsset* asset = sections ? sections(op.asset, assetError) : nullptr;
				if (asset == nullptr)
				{
					return fail(label + ": section asset '" + op.asset + "' is not available" + (assetError.empty() ? "." : ": " + assetError));
				}

				SectionPathParams params;
				for (const auto& [name, value] : op.params)
				{
					if (name == StampLengthParam || name == StampAnchorParam)
					{
						continue;
					}
					String text;
					String paramError;
					if (!SectionParamText(value, text))
					{
						return fail(label + " (section asset '" + op.asset + "'): section param '" + name + "' has an unsupported type.");
					}
					if (!SetSectionPathParam(params, name, text, paramError))
					{
						return fail(label + " (section asset '" + op.asset + "'): " + paramError);
					}
				}

				// A stamp with length 0 is laid out for the asset's length now that the asset is known (see StampLengthParam).
				PathSpline spline = in.spline;
				if (SettingFloat(op.params, StampLengthParam, -1.0f) == 0.0f && spline.points.size() == 2)
				{
					const SplinePoint first = spline.points[0];
					const SplinePoint last = spline.points[1];
					const float dx = last.x - first.x;
					const float dz = last.z - first.z;
					const float chord = std::hypot(dx, dz);
					if (chord > 1e-6f && asset->length > 0.0f)
					{
						const bool centered = SettingString(op.params, StampAnchorParam, "center") != "start";
						const float ux = dx / chord;
						const float uz = dz / chord;
						const float startX = centered ? (first.x + last.x) * 0.5f - ux * asset->length * 0.5f : first.x;
						const float startZ = centered ? (first.z + last.z) * 0.5f - uz * asset->length * 0.5f : first.z;
						spline.points[0].x = startX;
						spline.points[0].z = startZ;
						spline.points[1].x = startX + ux * asset->length;
						spline.points[1].z = startZ + uz * asset->length;
					}
				}

				std::vector<uint64> ids = in.pointIds;
				if (ids.size() != spline.points.size())
				{
					ids.clear();
					for (size_t i = 0; i < spline.points.size(); ++i)
					{
						ids.push_back(Hash(in.sourceId, static_cast<int64>(in.itemIndex), static_cast<int64>(i), 0x5ec7));
					}
				}

				String shapeError;
				if (!PrepareSectionPath(spline, ids, *asset, params, ground, prepared->shape, shapeError))
				{
					return fail(label + " (section asset '" + op.asset + "'): " + shapeError);
				}
				prepared->bounds = GetInfluenceRect(prepared->shape);
				prepared->remapLayers = std::any_of(asset->layerTextures.begin(), asset->layerTextures.end(),
					[](const String& t) { return !t.empty(); });
				prepared->sectionAsset = op.asset;
				for (const SectionVariant& variant : asset->variants)
				{
					const SectionPaint& paint = variant.paint;
					const size_t count = std::min(paint.packed.size(), paint.coverage.size());
					for (size_t i = 0; i < count; ++i)
					{
						if (paint.coverage[i] == 0)
						{
							continue;
						}
						for (size_t layer = 0; layer < 4; ++layer)
						{
							if (((paint.packed[i] >> (layer * 8)) & 0xFFu) != 0)
							{
								prepared->paintedLayers[layer] = true;
							}
						}
					}
				}
				if (prepared->shape.params.massEnabled && prepared->shape.massLayer >= 0 && prepared->shape.massLayer <= 3)
				{
					prepared->paintedLayers[static_cast<size_t>(prepared->shape.massLayer)] = true;
				}

				// Exclusions go through PrepareControl, so the inside / near tests are exactly those of Difference.
				NodeOutput exclusionNotes;
				for (const ExclusionShape& shape : in.exclusions)
				{
					if (!shape.area && !(shape.radius > 0.0f && std::isfinite(shape.radius)))
					{
						continue;
					}
					auto item = std::make_unique<DataItem>();
					item->kind = shape.area ? ItemKind::Area : ItemKind::Spline;
					item->spline = shape.spline;
					if (shape.area)
					{
						PreparedControl control;
						if (PrepareControl(exclusionNotes, *item, control))
						{
							prepared->exclusionAreas.push_back(std::move(control));
							prepared->exclusionItems.push_back(std::move(item));
						}
					}
					else
					{
						SplineTarget target;
						if (PrepareControl(exclusionNotes, *item, target.control))
						{
							target.radius = shape.radius;
							prepared->exclusionSplines.push_back(std::move(target));
							prepared->exclusionItems.push_back(std::move(item));
						}
					}
				}
				for (const String& warning : exclusionNotes.warnings)
				{
					Warn(label + " (section asset '" + op.asset + "'), Exclude: " + warning + ".");
				}
				break;
			}
			case TerrainOpType::Noise:
			{
				const SampledPath path(in.spline, EvaluationStep(in.spline, SamplingStep));
				prepared->amplitude = SettingFloat(op.params, "amplitude", 1.5f);
				const float wavelength = SettingFloat(op.params, "wavelength", 24.0f);
				prepared->frequency = wavelength > 0.0f ? 1.0f / wavelength : 0.0f;
				prepared->octaves = static_cast<uint32>(std::min(8.0f, std::max(1.0f, SettingFloat(op.params, "octaves", 3.0f))));
				prepared->falloff = std::max(0.0f, SettingFloat(op.params, "falloff", 8.0f));
				prepared->noiseSeed = Hash(kNoiseSeed, NoiseSeedParam(op));
				prepared->polygon = MakePolygon(path);
				prepared->edge.Build(path.GetSamples(), true, prepared->falloff);
				prepared->bounds = PathRect(path);
				break;
			}
			case TerrainOpType::Raise:
			{
				const SampledPath path(in.spline, EvaluationStep(in.spline, SamplingStep));
				prepared->width = SettingFloat(op.params, "width", 40.0f);
				prepared->falloffPower = std::max(0.01f, SettingFloat(op.params, "falloffPower", 2.0f));
				if (prepared->width <= 0.0f)
				{
					Warn(label + ": skipped, its width is not positive.");
					continue;
				}
				for (const SplineSample& s : path.GetSamples())
				{
					prepared->peaks.push_back(std::max(0.0f, s.y - ground(s.x, s.z)));
				}
				prepared->edge.Build(path.GetSamples(), path.IsClosed(), prepared->width);
				prepared->bounds = Grow(PathRect(path), prepared->width);
				break;
			}
			case TerrainOpType::Carve:
			{
				const SampledPath path(in.spline, EvaluationStep(in.spline, SamplingStep));
				prepared->width = std::max(0.0f, SettingFloat(op.params, "width", 4.0f));
				prepared->depth = SettingFloat(op.params, "depth", 1.5f);
				prepared->bank = std::max(0.0f, SettingFloat(op.params, "bank", 3.0f));
				const float reach = prepared->width + prepared->bank;
				if (reach <= 0.0f)
				{
					Warn(label + ": skipped, its width and bank are 0.");
					continue;
				}
				prepared->edge.Build(path.GetSamples(), path.IsClosed(), reach);
				prepared->bounds = Grow(PathRect(path), reach);
				break;
			}
			case TerrainOpType::Paint:
			{
				prepared->textureKey = SettingString(op.params, "texture", String());
				if (prepared->textureKey.empty())
				{
					Warn(label + ": skipped, it names no texture.");
					continue;
				}
				const SampledPath path(in.spline, EvaluationStep(in.spline, SamplingStep));
				prepared->weight = Saturate(SettingFloat(op.params, "weight", 1.0f));
				prepared->falloff = std::max(0.0f, SettingFloat(op.params, "falloff", 2.0f));
				prepared->area = in.spline.closed;
				prepared->tileMaterial = SettingString(op.params, "tileMaterial", String());
				if (prepared->area)
				{
					prepared->polygon = MakePolygon(path);
					prepared->edge.Build(path.GetSamples(), true, prepared->falloff);
					prepared->bounds = PathRect(path);
				}
				else
				{
					prepared->width = std::max(0.0f, SettingFloat(op.params, "width", 4.0f));
					if (prepared->width <= 0.0f)
					{
						Warn(label + ": skipped, its width is not positive.");
						continue;
					}
					prepared->edge.Build(path.GetSamples(), false, prepared->width);
					prepared->bounds = Grow(PathRect(path), prepared->width);
				}
				break;
			}
			case TerrainOpType::WaterArea:
			{
				const SampledPath path(in.spline, EvaluationStep(in.spline, SamplingStep));
				prepared->level = SettingFloat(op.params, "level", 0.0f) + SettingFloat(op.params, "levelOffset", 0.0f);
				prepared->margin = std::max(0.0f, SettingFloat(op.params, "margin", 4.0f));
				prepared->polygon = MakePolygon(path);
				prepared->edge.Build(path.GetSamples(), true, prepared->margin);
				prepared->bounds = Grow(PathRect(path), prepared->margin);
				prepared->depth = std::min(200.0f, std::max(0.0f, SettingFloat(op.params, "depth", 0.0f)));
				if (prepared->depth > 0.0f)
				{
					prepared->shoreWidth = std::max(0.5f, SettingFloat(op.params, "shoreWidth", 6.0f));
					prepared->basinEdge.Build(path.GetSamples(), true, prepared->shoreWidth);
					prepared->centroid = AreaCentroid(path.GetSamples());
					const float lowest = LowestRim(path.GetSamples(), ground);
					if (lowest < prepared->level)
					{
						Warn("WaterArea (node " + std::to_string(op.order) + ", control " + std::to_string(in.sourceId)
							+ "): base rim drops to " + FormatMeters(lowest) + " m, below the water level "
							+ FormatMeters(prepared->level) + " m; water will show past the shore");
					}
				}
				if (!op.asset.empty())
				{
					if (m_waterMaterial.empty())
					{
						m_waterMaterial = op.asset;
					}
					else if (m_waterMaterial != op.asset)
					{
						Warn(label + ": water material '" + op.asset + "' differs from '" + m_waterMaterial + "', which is used.");
					}
				}
				break;
			}
			case TerrainOpType::River:
			{
				const SampledPath path(in.spline, EvaluationStep(in.spline, SamplingStep));
				prepared->width = std::max(0.0f, SettingFloat(op.params, "width", 5.0f));
				prepared->bank = std::max(0.0f, SettingFloat(op.params, "bank", 4.0f));
				prepared->depth = std::max(0.0f, SettingFloat(op.params, "depth", 1.5f));
				prepared->margin = std::max(0.0f, SettingFloat(op.params, "waterMargin", 3.0f));
				const float surfaceOffset = SettingFloat(op.params, "surfaceOffset", -0.4f);
				// The bank's top holds to the end of the water and fades back to the ground over another bank.
				const float reach = prepared->WaterReach() + prepared->bank;
				const float length = path.GetLength();
				if (!(reach > 0.0f) || !std::isfinite(reach))
				{
					Warn(label + ": skipped, its width, bank and water margin are 0.");
					continue;
				}
				if (!(length > 0.0f))
				{
					Warn(label + ": skipped, its spline has no length.");
					continue;
				}

				// Water surface: the running minimum of the spline height plus the offset, sampled downstream.
				float step = kRiverSurfaceStep;
				while (length / step > kMaxRiverSurfaceSamples)
				{
					step *= 2.0f;
				}
				prepared->surfaceStep = step;
				const size_t whole = static_cast<size_t>(std::floor(length / step));
				const size_t count = whole + 1 + (length - static_cast<float>(whole) * step > 1e-3f ? 1 : 0);
				float running = 0.0f;
				float climb = 0.0f;
				prepared->surface.reserve(count);
				for (size_t i = 0; i < count; ++i)
				{
					const float distance = std::min(length, static_cast<float>(i) * step);
					SplineSample s = path.SampleAtDistance(distance);
					const float level = s.y + surfaceOffset;
					running = i == 0 ? level : std::min(running, level);
					climb = std::max(climb, level - running);
					s.distance = distance;
					s.y = running;
					prepared->surface.push_back(s);

					float nx = 0.0f;
					float nz = 0.0f;
					path.NormalAtDistance(distance, nx, nz);
					const float bed = running - prepared->depth;
					prepared->bedEdges[0].emplace_back(s.x + nx * prepared->width, bed, s.z + nz * prepared->width);
					prepared->bedEdges[1].emplace_back(s.x - nx * prepared->width, bed, s.z - nz * prepared->width);
				}
				if (climb > kRiverClimbTolerance)
				{
					Warn("River (node " + std::to_string(op.order) + ", control " + std::to_string(in.sourceId)
						+ "): the spline climbs " + FormatMeters(climb)
						+ " m above the water surface upstream of it; the surface stays flat there (is the river drawn upstream-first?)");
				}

				prepared->edge.Build(path.GetSamples(), false, reach);
				prepared->bounds = Grow(PathRect(path), reach);

				// River materials win over WaterArea ones: only they read the flow (see GetWaterMaterial).
				if (!op.asset.empty())
				{
					if (!m_waterMaterialFromRiver)
					{
						if (!m_waterMaterial.empty() && m_waterMaterial != op.asset)
						{
							Warn(label + ": water material '" + op.asset + "' is used instead of the WaterArea material '" + m_waterMaterial
								+ "' (river materials win).");
						}
						m_waterMaterial = op.asset;
						m_waterMaterialFromRiver = true;
					}
					else if (m_waterMaterial != op.asset)
					{
						Warn(label + ": water material '" + op.asset + "' differs from '" + m_waterMaterial + "', which is used.");
					}
				}
				break;
			}
			}

			m_ops.push_back(std::move(prepared));
		}

		if (!m_ops.empty())
		{
			WorldRect u = m_ops.front()->bounds;
			for (const auto& op : m_ops)
			{
				u.minX = std::min(u.minX, op->bounds.minX);
				u.minZ = std::min(u.minZ, op->bounds.minZ);
				u.maxX = std::max(u.maxX, op->bounds.maxX);
				u.maxZ = std::max(u.maxZ, op->bounds.maxZ);
			}
			const WorldRect& r = m_region->bounds;
			m_influence.minX = std::max(u.minX, r.minX);
			m_influence.minZ = std::max(u.minZ, r.minZ);
			m_influence.maxX = std::min(u.maxX, r.maxX);
			m_influence.maxZ = std::min(u.maxZ, r.maxZ);
		}
		return true;
	}

	void PreparedTerrain::GetOutlines(std::vector<TerrainOpOutline>& out) const
	{
		out.clear();
		for (const auto& op : m_ops)
		{
			TerrainOpOutline outline;
			outline.type = op->type;
			outline.order = op->order;
			const std::vector<SplineSample>* samples = nullptr;
			if (op->type == TerrainOpType::SectionPath)
			{
				if (op->shape.path)
				{
					samples = &op->shape.path->GetSamples();
					outline.closed = op->shape.path->IsClosed();
				}
				GetSectionCopyRects(op->shape, outline.copyRects);
			}
			else if (op->type == TerrainOpType::River)
			{
				samples = &op->surface;
				outline.closed = false;
				outline.bedEdges.assign(op->bedEdges.begin(), op->bedEdges.end());
			}
			else
			{
				samples = &op->edge.GetSamples();
				outline.closed = op->edge.IsClosed();
				if (op->type == TerrainOpType::WaterArea && op->depth > 0.0f)
				{
					outline.depth = op->depth;
					outline.level = op->level;
					outline.centroid = op->centroid;
				}
			}
			if (samples != nullptr)
			{
				outline.points.reserve(samples->size());
				for (const SplineSample& s : *samples)
				{
					outline.points.emplace_back(s.x, s.y, s.z);
				}
			}
			out.push_back(std::move(outline));
		}
	}

	WorldRect PreparedTerrain::GetInfluence() const
	{
		return m_influence;
	}

	bool PreparedTerrain::IsEmpty() const
	{
		return m_ops.empty();
	}

	float PreparedTerrain::RegionFade(const float x, const float z) const
	{
		if (!m_region || !Contains(m_region->bounds, x, z) || !m_region->polygon->Contains(x, z))
		{
			return 0.0f;
		}
		NearestHit hit;
		if (m_borderFade > 0.0f && m_region->edge.Nearest(x, z, hit))
		{
			return Smoothstep01(hit.distance / m_borderFade);
		}
		return 1.0f;
	}

	TerrainTarget PreparedTerrain::Evaluate(const float x, const float z, const float baseHeight, const TextureLayerLookup& layers,
		TerrainEvaluateNotes* notes, const terrain_channel::Type channels) const
	{
		TerrainTarget out;
		if ((channels & terrain_channel::Material) != 0)
		{
			// The tile's resolved material, wherever in the tile (x, z) lies (the material pass samples tile centres).
			if (const String* material = ResolvedTileMaterial(x, z))
			{
				out.material = *material;
			}
		}
		const bool wantHeights = (channels & terrain_channel::Heights) != 0;
		const bool wantSplat = (channels & terrain_channel::Splat) != 0;
		const bool wantWater = (channels & terrain_channel::Water) != 0;
		if (m_ops.empty() || !(wantHeights || wantSplat || wantWater) || !Contains(m_influence, x, z))
		{
			return out;
		}
		const float fade = RegionFade(x, z);
		if (fade <= 0.0f)
		{
			return out;
		}

		float h = baseHeight;
		bool touched = false;
		std::array<double, 4> paint{};
		double paintAlpha = 0.0;

		// Section layer to tile layer mapping of the SectionPath op being evaluated. The remap function captures only a
		// reference to this state, so it fits std::function's small buffer (no allocation per call).
		struct SectionRemapState
		{
			const TextureLayerLookup* layers = nullptr;
			TerrainEvaluateNotes* notes = nullptr;
			const PreparedOp* op = nullptr;
			size_t opIndex = 0;
		};
		SectionRemapState remapState;
		remapState.layers = &layers;
		remapState.notes = notes;
		LayerRemapFunction remap;
		if (wantSplat && layers)
		{
			remap = [&remapState](const float px, const float pz)
			{
				// Only called where the section paints.
				const PreparedOp& op = *remapState.op;
				std::array<int8, 4> r{};
				for (size_t i = 0; i < 4; ++i)
				{
					const String& texture = op.shape.asset.layerTextures[i];
					r[i] = texture.empty() ? static_cast<int8>(-1) : (*remapState.layers)(px, pz, texture);
					if (r[i] < 0 && remapState.notes && !texture.empty() && op.paintedLayers[i])
					{
						remapState.notes->missingSectionLayers.emplace(remapState.opIndex, static_cast<uint8>(i));
					}
				}
				return r;
			};
		}
		const LayerRemapFunction noRemap;
		auto blendHeight = [&h, &touched, fade](const float target, const float weight)
		{
			const float w = std::min(1.0f, weight) * fade;
			if (w > 0.0f)
			{
				h += (target - h) * w;
				touched = true;
			}
		};

		for (size_t opIndex = 0; opIndex < m_ops.size(); ++opIndex)
		{
			const PreparedOp& op = *m_ops[opIndex];
			if (!Contains(op.bounds, x, z))
			{
				continue;
			}

			switch (op.type)
			{
			case TerrainOpType::SectionPath:
			{
				if (!wantHeights && !wantSplat)
				{
					break;
				}
				// The section's paint does not depend on the running height, so a splat-only pass, which leaves h at the
				// base, paints the same.
				remapState.op = &op;
				remapState.opIndex = opIndex;
				const TerrainTarget t = EvaluateSectionTerrain(op.shape, x, z, h, op.remapLayers ? remap : noRemap);
				if (wantHeights && t.heightWeight > 0.0f)
				{
					blendHeight(t.height, t.heightWeight);
				}
				if (wantSplat && t.splatWeight > 0.0f)
				{
					CompositeSplat(paint, paintAlpha, t.splat, std::min(1.0f, t.splatWeight) * fade);
				}
				break;
			}
			case TerrainOpType::Noise:
			{
				if (!wantHeights || !op.polygon->Contains(x, z))
				{
					break;
				}
				NearestHit hit;
				const float weight = op.falloff > 0.0f && op.edge.Nearest(x, z, hit) ? Smoothstep01(hit.distance / op.falloff) : 1.0f;
				if (weight > 0.0f)
				{
					const float noise = FractalNoise(op.noiseSeed, x, z, op.frequency, op.octaves, 0.5f);
					blendHeight(h + op.amplitude * noise, weight);
				}
				break;
			}
			case TerrainOpType::Raise:
			{
				NearestHit hit;
				if (!wantHeights || !op.edge.Nearest(x, z, hit))
				{
					break;
				}
				const float peak = op.peaks[hit.a] + (op.peaks[hit.b] - op.peaks[hit.a]) * hit.t;
				const float profile = std::pow(Smoothstep01(1.0f - hit.distance / op.width), op.falloffPower);
				if (profile > 0.0f)
				{
					blendHeight(h + peak * profile, 1.0f);
				}
				break;
			}
			case TerrainOpType::Carve:
			{
				NearestHit hit;
				if (!wantHeights || !op.edge.Nearest(x, z, hit))
				{
					break;
				}
				float profile = 1.0f;
				if (hit.distance > op.width)
				{
					profile = op.bank > 0.0f ? 1.0f - Smoothstep01((hit.distance - op.width) / op.bank) : 0.0f;
				}
				if (profile > 0.0f)
				{
					blendHeight(h - op.depth * profile, 1.0f);
				}
				break;
			}
			case TerrainOpType::Paint:
			{
				// An op that lost the tile to another material paints nothing there.
				if (!wantSplat || LostTile(op, x, z))
				{
					break;
				}
				float weight = PaintEdgeWeight(op, x, z);
				weight *= op.weight * fade;
				if (weight <= 0.0f)
				{
					break;
				}
				const int8 layer = layers ? layers(x, z, op.textureKey) : static_cast<int8>(-1);
				if (layer < 0 || layer > 3)
				{
					if (notes)
					{
						notes->missingLayerOps.insert(opIndex);
					}
					break;
				}
				std::array<uint8, 4> splat{};
				splat[static_cast<size_t>(layer)] = 255;
				CompositeSplat(paint, paintAlpha, splat, weight);
				break;
			}
			case TerrainOpType::WaterArea:
			{
				NearestHit hit;
				if (wantHeights && op.depth > 0.0f && op.polygon->Contains(x, z))
				{
					// Inward distance d; past the shore width the reach finds nothing and the bed holds.
					const float s = op.basinEdge.Nearest(x, z, hit) ? Smoothstep01(hit.distance / op.shoreWidth) : 1.0f;
					blendHeight(std::min(h, h + (op.level - op.depth - h) * s), 1.0f);
				}
				if (wantWater && (op.polygon->Contains(x, z) || op.edge.Nearest(x, z, hit)))
				{
					out.waterType = 1;
					out.waterHeight = op.level;
				}
				break;
			}
			case TerrainOpType::River:
			{
				NearestHit hit;
				if ((!wantHeights && !wantWater) || !op.edge.Nearest(x, z, hit))
				{
					break;
				}
				const std::vector<SplineSample>& samples = op.edge.GetSamples();
				const float along = samples[hit.a].distance + (samples[hit.b].distance - samples[hit.a].distance) * hit.t;
				const float surface = op.SurfaceAt(along);

				// The downstream end is flat: past it there is no water and no owned bed (the river flows into something
				// there, a waterfall or a lake). Only the bank closes the bed, by the distance to the bed's end edge, and it
				// only ever lowers the running height. The upstream end closes like the banks, by the same distance.
				float past = 0.0f;
				float lateral = hit.distance;
				bool atStart = false;
				float distance = hit.distance;
				if (RiverPastEnd(samples, hit, x, z, past, lateral, atStart))
				{
					const float d = std::hypot(std::max(0.0f, lateral - op.width), past);
					if (!atStart)
					{
						if (wantHeights && op.bank > 0.0f && d < op.bank)
						{
							const float w = 1.0f - Smoothstep01(d / op.bank);
							blendHeight(std::min(h, h + (surface - op.depth - h) * w), 1.0f);
						}
						break;
					}
					distance = op.width + d;
				}

				if (wantHeights)
				{
					// Bed within width; over the bank it climbs to the ground, or to just above the water where the ground
					// is lower; that top holds to the end of the water and fades back to the ground over another bank.
					const float bed = surface - op.depth;
					const float top = surface + kRiverFreeboard;
					if (distance <= op.width)
					{
						blendHeight(bed, 1.0f);
					}
					else if (distance <= op.width + op.bank)
					{
						const float s = op.bank > 0.0f ? Smoothstep01((distance - op.width) / op.bank) : 1.0f;
						blendHeight(bed + (std::max(h, top) - bed) * s, 1.0f);
					}
					else if (h < top && distance < op.WaterReach() + op.bank)
					{
						const float w = op.bank > 0.0f ? 1.0f - Smoothstep01((distance - op.WaterReach()) / op.bank) : 1.0f;
						if (w > 0.0f)
						{
							blendHeight(h + (top - h) * w, 1.0f);
						}
					}
				}
				if (wantWater && distance <= op.WaterReach())
				{
					out.waterType = 1;
					out.waterHeight = surface;
				}
				break;
			}
			}
		}

		if (touched)
		{
			out.height = h;
			out.heightWeight = 1.0f;
		}
		if (paintAlpha > 0.0)
		{
			for (size_t i = 0; i < 4; ++i)
			{
				const double v = std::round(paint[i] / paintAlpha);
				out.splat[i] = static_cast<uint8>(std::min(255.0, std::max(0.0, v)));
			}
			out.splatWeight = static_cast<float>(std::min(1.0, paintAlpha));
		}
		return out;
	}

	void PreparedTerrain::DescribeNotes(const TerrainEvaluateNotes& notes, std::vector<String>& warnings) const
	{
		for (const size_t index : notes.missingLayerOps)
		{
			if (index < m_ops.size())
			{
				const PreparedOp& op = *m_ops[index];
				warnings.push_back("Paint op of node " + std::to_string(op.order) + ": texture '" + op.textureKey
					+ "' is not a layer of the terrain tile, skipped there.");
			}
		}

		// One warning per section op, listing its textures in layer order (the set is sorted by op, then layer).
		size_t current = m_ops.size();
		String textures;
		const auto flush = [this, &warnings, &current, &textures]()
		{
			if (current < m_ops.size() && !textures.empty())
			{
				const PreparedOp& op = *m_ops[current];
				warnings.push_back("SectionPath op of node " + std::to_string(op.order) + " (section asset '" + op.sectionAsset
					+ "'): section paint with texture(s) " + textures + " may be skipped on tiles that have no layer with that texture.");
			}
			textures.clear();
		};
		for (const auto& [index, layer] : notes.missingSectionLayers)
		{
			if (index >= m_ops.size() || layer > 3)
			{
				continue;
			}
			if (index != current)
			{
				flush();
				current = index;
			}
			textures += (textures.empty() ? "'" : ", '") + m_ops[index]->shape.asset.layerTextures[layer] + "'";
		}
		flush();

		warnings.insert(warnings.end(), m_tileMaterialConflicts.begin(), m_tileMaterialConflicts.end());
	}

	void PreparedTerrain::ResolveTileMaterials(const GridLayout& layout, const WorldRect& area)
	{
		m_tileLayout = layout;
		m_tileMaterials.clear();
		m_tileMaterialConflicts.clear();
		const double tileSize = layout.vertexSpacing * static_cast<double>(layout.verticesPerTile);
		if (!(tileSize > 0.0) || !std::isfinite(tileSize) || !m_region || !(area.minX <= area.maxX) || !(area.minZ <= area.maxZ))
		{
			return;
		}

		// Tiles with a positive overlap (a degenerate rect: the tile it lies in).
		const auto firstTile = [tileSize](const float min, const double origin)
		{
			return static_cast<int32>(std::floor((static_cast<double>(min) - origin) / tileSize));
		};
		const auto lastTile = [tileSize, &firstTile](const float min, const float max, const double origin)
		{
			return std::max(firstTile(min, origin), static_cast<int32>(std::ceil((static_cast<double>(max) - origin) / tileSize)) - 1);
		};
		const int32 areaX0 = firstTile(area.minX, layout.originX);
		const int32 areaX1 = lastTile(area.minX, area.maxX, layout.originX);
		const int32 areaZ0 = firstTile(area.minZ, layout.originZ);
		const int32 areaZ1 = lastTile(area.minZ, area.maxZ, layout.originZ);

		const auto tileRect = [&layout, tileSize](const int32 x, const int32 z)
		{
			WorldRect r;
			r.minX = static_cast<float>(layout.originX + x * tileSize) + kTileBorderTolerance;
			r.minZ = static_cast<float>(layout.originZ + z * tileSize) + kTileBorderTolerance;
			r.maxX = static_cast<float>(layout.originX + (x + 1) * tileSize) - kTileBorderTolerance;
			r.maxZ = static_cast<float>(layout.originZ + (z + 1) * tileSize) - kTileBorderTolerance;
			return r;
		};

		// Requested materials per tile in stack order, keyed (z, x) so the result comes out sorted; whether the tile
		// meets the region, decided once per tile.
		std::map<std::pair<int32, int32>, std::vector<String>> requested;
		std::map<std::pair<int32, int32>, bool> inRegion;
		for (const auto& op : m_ops)
		{
			if (op->type != TerrainOpType::Paint || op->tileMaterial.empty() || !(op->weight > 0.0f))
			{
				continue;
			}
			const int32 x0 = std::max(areaX0, firstTile(op->bounds.minX, layout.originX));
			const int32 x1 = std::min(areaX1, firstTile(op->bounds.maxX, layout.originX));
			const int32 z0 = std::max(areaZ0, firstTile(op->bounds.minZ, layout.originZ));
			const int32 z1 = std::min(areaZ1, firstTile(op->bounds.maxZ, layout.originZ));
			for (int32 z = z0; z <= z1; ++z)
			{
				for (int32 x = x0; x <= x1; ++x)
				{
					const WorldRect r = tileRect(x, z);
					const bool touches = op->area ? LoopTouchesRect(op->edge.GetSamples(), *op->polygon, r)
						: LineTouchesRect(op->edge.GetSamples(), op->width, r);
					if (!touches)
					{
						continue;
					}
					const auto key = std::make_pair(z, x);
					auto region = inRegion.find(key);
					if (region == inRegion.end())
					{
						region = inRegion.emplace(key, LoopTouchesRect(m_region->edge.GetSamples(), *m_region->polygon, r)).first;
					}
					if (region->second)
					{
						requested[key].push_back(op->tileMaterial);
					}
				}
			}
		}

		for (const auto& [key, materials] : requested)
		{
			const String& winner = materials.back();
			TileMaterialRequest request;
			request.tileX = key.second;
			request.tileZ = key.first;
			request.material = winner;
			m_tileMaterials.push_back(std::move(request));

			std::vector<String> losers;
			for (const String& material : materials)
			{
				if (material != winner && std::find(losers.begin(), losers.end(), material) == losers.end())
				{
					losers.push_back(material);
					m_tileMaterialConflicts.push_back("Tile (" + std::to_string(key.second) + ", " + std::to_string(key.first) + "): '"
						+ material + "' and '" + winner + "' both requested; '" + winner + "' wins");
				}
			}
		}
	}

	const std::vector<TileMaterialRequest>& PreparedTerrain::GetTileMaterials() const
	{
		return m_tileMaterials;
	}

	bool PreparedTerrain::IsTileMaterialPaint(const float x, const float z) const
	{
		if (m_ops.empty() || !Contains(m_influence, x, z))
		{
			return false;
		}
		const float fade = RegionFade(x, z);
		if (fade <= 0.0f)
		{
			return false;
		}
		for (const auto& op : m_ops)
		{
			if (op->type != TerrainOpType::Paint || op->tileMaterial.empty() || !Contains(op->bounds, x, z) || LostTile(*op, x, z))
			{
				continue;
			}
			if (PaintEdgeWeight(*op, x, z) * op->weight * fade > 0.0f)
			{
				return true;
			}
		}
		return false;
	}

	float PreparedTerrain::PaintEdgeWeight(const PreparedOp& op, const float x, const float z)
	{
		float weight = 0.0f;
		NearestHit hit;
		if (op.area)
		{
			if (op.polygon->Contains(x, z))
			{
				weight = op.falloff > 0.0f && op.edge.Nearest(x, z, hit) ? Smoothstep01(hit.distance / op.falloff) : 1.0f;
			}
		}
		else if (op.edge.Nearest(x, z, hit))
		{
			weight = op.falloff > 0.0f ? Smoothstep01((op.width - hit.distance) / op.falloff) : 1.0f;
		}
		return weight;
	}

	const String* PreparedTerrain::ResolvedTileMaterial(const float x, const float z) const
	{
		if (m_tileMaterials.empty())
		{
			return nullptr;
		}
		const double tileSize = m_tileLayout.vertexSpacing * static_cast<double>(m_tileLayout.verticesPerTile);
		const double fx = std::floor((static_cast<double>(x) - m_tileLayout.originX) / tileSize);
		const double fz = std::floor((static_cast<double>(z) - m_tileLayout.originZ) / tileSize);
		if (!(std::abs(fx) < 2.0e9) || !(std::abs(fz) < 2.0e9))
		{
			return nullptr;
		}
		const int32 tileX = static_cast<int32>(fx);
		const int32 tileZ = static_cast<int32>(fz);
		const auto it = std::lower_bound(m_tileMaterials.begin(), m_tileMaterials.end(), std::make_pair(tileZ, tileX),
			[](const TileMaterialRequest& r, const std::pair<int32, int32>& key)
			{
				return std::make_pair(r.tileZ, r.tileX) < key;
			});
		if (it == m_tileMaterials.end() || it->tileZ != tileZ || it->tileX != tileX)
		{
			return nullptr;
		}
		return &it->material;
	}

	bool PreparedTerrain::LostTile(const PreparedOp& op, const float x, const float z) const
	{
		if (op.tileMaterial.empty())
		{
			return false;
		}
		const String* material = ResolvedTileMaterial(x, z);
		return material != nullptr && *material != op.tileMaterial;
	}

	void PreparedTerrain::BuildPlacements(const std::function<float(float, float)>& ground, std::vector<Placement>& out,
		std::vector<String>* warnings) const
	{
		out.clear();
		std::vector<Placement> section;
		for (const auto& op : m_ops)
		{
			if (op->type != TerrainOpType::SectionPath)
			{
				continue;
			}
			PlacementExcludeFunction excluded;
			if (!op->exclusionItems.empty())
			{
				const PreparedOp& prepared = *op;
				excluded = [&prepared](const float x, const float z) { return prepared.Excluded(x, z); };
			}
			BuildSectionPlacements(op->shape, ground, section, excluded);
			for (Placement& p : section)
			{
				if (RegionFade(p.position.x, p.position.z) < 0.5f)
				{
					continue;
				}
				p.key = Hash(op->order, static_cast<int64>(op->sourceId), static_cast<int64>(p.key));
				out.push_back(std::move(p));
			}
		}

		std::stable_sort(out.begin(), out.end(), [](const Placement& a, const Placement& b) { return a.key < b.key; });
		const auto duplicate = std::unique(out.begin(), out.end(), [](const Placement& a, const Placement& b) { return a.key == b.key; });
		if (duplicate != out.end())
		{
			if (warnings)
			{
				warnings->push_back("Section placements with duplicate keys were dropped.");
			}
			out.erase(duplicate, out.end());
		}
	}

	const String& PreparedTerrain::GetWaterMaterial() const
	{
		return m_waterMaterial;
	}

	void PreparedTerrain::GetFlowPaths(std::vector<terrain_io::water_flow::FlowPath>& out) const
	{
		out.clear();
		terrain_io::water_flow::FlowPath run;
		const auto flush = [&out, &run]()
		{
			if (run.size() >= 2)
			{
				out.push_back(run);
			}
			run.clear();
		};
		for (const auto& op : m_ops)
		{
			if (op->type != TerrainOpType::River)
			{
				continue;
			}
			const auto visit = [this, &op, &run, &flush](const SplineSample& s)
			{
				if (RegionFade(s.x, s.z) > 0.0f)
				{
					terrain_io::water_flow::FlowPoint point;
					point.x = s.x;
					point.z = s.z;
					point.halfWidth = op->WaterReach();
					run.push_back(point);
				}
				else
				{
					flush();
				}
			};
			// Every kRiverFlowStride-th surface sample, then the end of the river.
			const size_t count = op->surface.size();
			for (size_t i = 0; i < count; i += kRiverFlowStride)
			{
				visit(op->surface[i]);
			}
			if (count > 0 && (count - 1) % kRiverFlowStride != 0)
			{
				visit(op->surface.back());
			}
			flush();
		}
	}

	const std::vector<String>& PreparedTerrain::GetWarnings() const
	{
		return m_warnings;
	}

	void PreparedTerrain::Warn(const String& message)
	{
		if (std::find(m_warnings.begin(), m_warnings.end(), message) == m_warnings.end())
		{
			m_warnings.push_back(message);
		}
	}
}
