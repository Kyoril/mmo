// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "terrain_compose.h"

#include "node_util.h"

#include "pcg/pcg_random.h"
#include "pcg/section_path.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <tuple>
#include <utility>

namespace mmo::pcg::graph
{
	namespace
	{
		/// Seed of the Noise op's fractal noise (combined with the op order).
		constexpr uint64 kNoiseSeed = 0x7e44a1f3c0de5eedull;

		/// Largest grid size (cells per axis) of a PolylineIndex.
		constexpr double kMaxIndexCells = 256.0;

		float Saturate(const float v)
		{
			return std::min(1.0f, std::max(0.0f, v));
		}

		float Smoothstep01(float t)
		{
			t = Saturate(t);
			return t * t * (3.0f - 2.0f * t);
		}

		/// Result of a nearest-segment query.
		struct NearestHit
		{
			float distance = 0.0f;
			/// Sample indices of the segment's ends and the parameter of the nearest point between them.
			uint32 a = 0;
			uint32 b = 0;
			float t = 0.0f;
		};

		/// Nearest segment of a polyline within a fixed reach, with the segments bucketed into a uniform grid: every
		/// cell lists (ascending) the segments whose bounds grown by the reach touch it, so the cell holding a point
		/// lists every segment within reach of it. Exact: the minimum over the listed segments, ties to the lowest
		/// segment index.
		class PolylineIndex
		{
		public:
			void Build(const std::vector<SplineSample>& samples, const bool closed, const float reach)
			{
				m_samples = samples;
				m_reach = std::max(0.0f, reach);
				m_segments.clear();
				m_cellStart.clear();
				m_cells.clear();
				m_columns = 0;
				m_rows = 0;
				if (m_samples.empty())
				{
					return;
				}

				const uint32 n = static_cast<uint32>(m_samples.size());
				if (n == 1)
				{
					m_segments.emplace_back(0, 0);
				}
				for (uint32 i = 0; i + 1 < n; ++i)
				{
					m_segments.emplace_back(i, i + 1);
				}
				if (closed && n >= 3)
				{
					m_segments.emplace_back(n - 1, 0);
				}

				double minX = m_samples[0].x;
				double maxX = minX;
				double minZ = m_samples[0].z;
				double maxZ = minZ;
				for (const SplineSample& s : m_samples)
				{
					minX = std::min(minX, static_cast<double>(s.x));
					maxX = std::max(maxX, static_cast<double>(s.x));
					minZ = std::min(minZ, static_cast<double>(s.z));
					maxZ = std::max(maxZ, static_cast<double>(s.z));
				}
				minX -= m_reach;
				minZ -= m_reach;
				maxX += m_reach;
				maxZ += m_reach;
				const double extent = std::max(maxX - minX, maxZ - minZ);
				m_cellSize = std::max({ static_cast<double>(m_reach), extent / kMaxIndexCells, 0.5 });
				m_originX = minX;
				m_originZ = minZ;
				m_columns = static_cast<int64>(std::floor((maxX - minX) / m_cellSize)) + 1;
				m_rows = static_cast<int64>(std::floor((maxZ - minZ) / m_cellSize)) + 1;

				// Two passes: count, then fill (CSR, segments ascending per cell).
				const size_t cellCount = static_cast<size_t>(m_columns * m_rows);
				std::vector<uint32> counts(cellCount, 0);
				for (int pass = 0; pass < 2; ++pass)
				{
					std::vector<uint32> fill;
					if (pass == 1)
					{
						m_cellStart.assign(cellCount + 1, 0);
						for (size_t c = 0; c < cellCount; ++c)
						{
							m_cellStart[c + 1] = m_cellStart[c] + counts[c];
						}
						m_cells.resize(m_cellStart[cellCount]);
						fill.assign(m_cellStart.begin(), m_cellStart.end() - 1);
					}
					for (uint32 s = 0; s < static_cast<uint32>(m_segments.size()); ++s)
					{
						const SplineSample& a = m_samples[m_segments[s].first];
						const SplineSample& b = m_samples[m_segments[s].second];
						const int64 x0 = CellOf(std::min(a.x, b.x) - m_reach, m_originX, m_columns);
						const int64 x1 = CellOf(std::max(a.x, b.x) + m_reach, m_originX, m_columns);
						const int64 z0 = CellOf(std::min(a.z, b.z) - m_reach, m_originZ, m_rows);
						const int64 z1 = CellOf(std::max(a.z, b.z) + m_reach, m_originZ, m_rows);
						for (int64 z = z0; z <= z1; ++z)
						{
							for (int64 x = x0; x <= x1; ++x)
							{
								const size_t c = static_cast<size_t>(z * m_columns + x);
								if (pass == 0)
								{
									++counts[c];
								}
								else
								{
									m_cells[fill[c]++] = s;
								}
							}
						}
					}
				}
			}

			/// Nearest segment within the reach of (x, z); false if there is none.
			bool Nearest(const float x, const float z, NearestHit& hit) const
			{
				if (m_columns == 0)
				{
					return false;
				}
				const double cx = std::floor((x - m_originX) / m_cellSize);
				const double cz = std::floor((z - m_originZ) / m_cellSize);
				if (!(cx >= 0.0 && cz >= 0.0 && cx < static_cast<double>(m_columns) && cz < static_cast<double>(m_rows)))
				{
					return false;
				}
				const size_t c = static_cast<size_t>(static_cast<int64>(cz) * m_columns + static_cast<int64>(cx));
				bool found = false;
				double best = 0.0;
				for (uint32 i = m_cellStart[c]; i < m_cellStart[c + 1]; ++i)
				{
					const std::pair<uint32, uint32>& seg = m_segments[m_cells[i]];
					const SplineSample& a = m_samples[seg.first];
					const SplineSample& b = m_samples[seg.second];
					const double dx = static_cast<double>(b.x) - a.x;
					const double dz = static_cast<double>(b.z) - a.z;
					const double len2 = dx * dx + dz * dz;
					double t = 0.0;
					if (len2 > 0.0)
					{
						t = std::min(1.0, std::max(0.0, ((x - a.x) * dx + (z - a.z) * dz) / len2));
					}
					const double px = x - (a.x + dx * t);
					const double pz = z - (a.z + dz * t);
					const double d = std::sqrt(px * px + pz * pz);
					if (d <= m_reach && (!found || d < best))
					{
						found = true;
						best = d;
						hit.a = seg.first;
						hit.b = seg.second;
						hit.t = static_cast<float>(t);
					}
				}
				hit.distance = static_cast<float>(best);
				return found;
			}

		private:
			int64 CellOf(const double v, const double origin, const int64 count) const
			{
				const int64 c = static_cast<int64>(std::floor((v - origin) / m_cellSize));
				return std::min(count - 1, std::max<int64>(0, c));
			}

		private:
			std::vector<SplineSample> m_samples;
			std::vector<std::pair<uint32, uint32>> m_segments;
			std::vector<uint32> m_cellStart;
			std::vector<uint32> m_cells;
			float m_reach = 0.0f;
			double m_cellSize = 1.0;
			double m_originX = 0.0;
			double m_originZ = 0.0;
			int64 m_columns = 0;
			int64 m_rows = 0;
		};

		bool Contains(const WorldRect& r, const float x, const float z)
		{
			return x >= r.minX && x <= r.maxX && z >= r.minZ && z <= r.maxZ;
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

		/// Containment index of a closed path, rows as PrepareControl builds them.
		std::unique_ptr<RowIndexedPolygon> MakePolygon(const SampledPath& path)
		{
			const WorldRect r = PathRect(path);
			return std::make_unique<RowIndexedPolygon>(path.GetSamples(), std::max(1.0f, (r.maxZ - r.minZ) / 4096.0f));
		}

		/// Converts an attribute value to the text SetSectionPathParam parses.
		bool ToParamText(const AttributeValue& value, String& out)
		{
			char buffer[64];
			switch (TypeOf(value))
			{
			case AttributeType::Float:
				std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(std::get<float>(value)));
				out = buffer;
				return true;
			case AttributeType::Int:
				std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(std::get<int64>(value)));
				out = buffer;
				return true;
			case AttributeType::Bool:
				out = std::get<bool>(value) ? "1" : "0";
				return true;
			case AttributeType::String:
				out = std::get<String>(value);
				return true;
			default:
				return false;
			}
		}

		String NodeLabel(const TerrainOpInput& in)
		{
			return String(TerrainOpTypeName(in.op.type)) + " op of node " + std::to_string(in.op.order);
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

		// Noise, Paint (areas), region: inward fade distance. Raise: width; Carve: half bed width and bank.
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
		mutable bool warnedMissingLayer = false;

		// WaterArea.
		float level = 0.0f;
		float margin = 0.0f;

		// SectionPath.
		SectionPathShape shape;
		bool remapLayers = false;
	};

	std::vector<TerrainOpInput> CollectTerrainOps(const Graph& graph, const EvalResult& result)
	{
		std::set<uint32> nodeIds;
		for (const NodeInstance& node : graph.nodes)
		{
			nodeIds.insert(node.id);
		}

		std::vector<std::tuple<uint32, uint32, size_t, TerrainOpInput>> gathered;
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
				gathered.emplace_back(item.op.order, id, i, std::move(in));
			}
		}

		std::stable_sort(gathered.begin(), gathered.end(), [](const auto& a, const auto& b)
		{
			return std::tie(std::get<0>(a), std::get<1>(a), std::get<2>(a)) < std::tie(std::get<0>(b), std::get<1>(b), std::get<2>(b));
		});

		std::vector<TerrainOpInput> out;
		out.reserve(gathered.size());
		for (auto& g : gathered)
		{
			out.push_back(std::move(std::get<3>(g)));
		}
		return out;
	}

	PreparedTerrain::PreparedTerrain() = default;
	PreparedTerrain::~PreparedTerrain() = default;
	PreparedTerrain::PreparedTerrain(PreparedTerrain&&) noexcept = default;
	PreparedTerrain& PreparedTerrain::operator=(PreparedTerrain&&) noexcept = default;

	bool PreparedTerrain::Prepare(const std::vector<TerrainOpInput>& ops, const std::function<float(float, float)>& baseGround,
		const SectionAssetProvider& sections, const PathSpline& region, const float borderFade, String& error)
	{
		m_ops.clear();
		m_region.reset();
		m_influence = WorldRect{};
		m_waterMaterial.clear();
		m_warnings.clear();
		m_borderFade = std::isfinite(borderFade) ? std::max(0.0f, borderFade) : 0.0f;
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
			prepared->edge.Build(path.GetSamples(), true, m_borderFade);
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
			const size_t minPoints = needsArea || (op.type == TerrainOpType::Paint && in.spline.closed) ? 3u
				: (op.type == TerrainOpType::SectionPath ? 2u : 1u);
			const bool saneGeometry = in.spline.points.size() >= minPoints && SaneBounds(SplineBounds(in.spline));
			if (op.type == TerrainOpType::SectionPath)
			{
				if (!saneGeometry)
				{
					return fail(label + ": section path '" + op.asset + "' needs at least 2 points with sane coordinates.");
				}
			}
			else if (!saneGeometry || (needsArea && !in.spline.closed))
			{
				Warn(label + ": skipped, its geometry is not " + (needsArea ? "a closed area" : "a usable spline") + ".");
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
					String text;
					String paramError;
					if (!ToParamText(value, text))
					{
						return fail(label + " (section asset '" + op.asset + "'): section param '" + name + "' has an unsupported type.");
					}
					if (!SetSectionPathParam(params, name, text, paramError))
					{
						return fail(label + " (section asset '" + op.asset + "'): " + paramError);
					}
				}

				std::vector<uint64> ids = in.pointIds;
				if (ids.size() != in.spline.points.size())
				{
					ids.clear();
					for (size_t i = 0; i < in.spline.points.size(); ++i)
					{
						ids.push_back(Hash(in.sourceId, static_cast<int64>(i), 0, 0x5ec7));
					}
				}

				String shapeError;
				if (!PrepareSectionPath(in.spline, ids, *asset, params, ground, prepared->shape, shapeError))
				{
					return fail(label + " (section asset '" + op.asset + "'): " + shapeError);
				}
				prepared->bounds = GetInfluenceRect(prepared->shape);
				prepared->remapLayers = std::any_of(asset->layerTextures.begin(), asset->layerTextures.end(),
					[](const String& t) { return !t.empty(); });
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
				prepared->noiseSeed = Hash(kNoiseSeed, static_cast<int64>(op.order));
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
				prepared->width = std::max(0.0f, SettingFloat(op.params, "width", 4.0f)) * 0.5f;
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

	TerrainTarget PreparedTerrain::Evaluate(const float x, const float z, const float baseHeight, const TextureLayerLookup& layers) const
	{
		TerrainTarget out;
		if (m_ops.empty() || !Contains(m_influence, x, z))
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
		auto blendHeight = [&h, &touched, fade](const float target, const float weight)
		{
			const float w = std::min(1.0f, weight) * fade;
			if (w > 0.0f)
			{
				h += (target - h) * w;
				touched = true;
			}
		};

		for (const auto& opPtr : m_ops)
		{
			const PreparedOp& op = *opPtr;
			if (!Contains(op.bounds, x, z))
			{
				continue;
			}

			switch (op.type)
			{
			case TerrainOpType::SectionPath:
			{
				LayerRemapFunction remap;
				if (op.remapLayers && layers)
				{
					remap = [&op, &layers](const float px, const float pz)
					{
						std::array<int8, 4> r{};
						for (size_t i = 0; i < 4; ++i)
						{
							const String& texture = op.shape.asset.layerTextures[i];
							r[i] = texture.empty() ? static_cast<int8>(-1) : layers(px, pz, texture);
						}
						return r;
					};
				}
				const TerrainTarget t = EvaluateSectionTerrain(op.shape, x, z, h, remap);
				if (t.heightWeight > 0.0f)
				{
					blendHeight(t.height, t.heightWeight);
				}
				if (t.splatWeight > 0.0f)
				{
					CompositeSplat(paint, paintAlpha, t.splat, std::min(1.0f, t.splatWeight) * fade);
				}
				break;
			}
			case TerrainOpType::Noise:
			{
				if (!op.polygon->Contains(x, z))
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
				if (!op.edge.Nearest(x, z, hit))
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
				if (!op.edge.Nearest(x, z, hit))
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
				weight *= op.weight * fade;
				if (weight <= 0.0f)
				{
					break;
				}
				const int8 layer = layers ? layers(x, z, op.textureKey) : static_cast<int8>(-1);
				if (layer < 0 || layer > 3)
				{
					if (!op.warnedMissingLayer)
					{
						op.warnedMissingLayer = true;
						Warn("PaintSplat op of node " + std::to_string(op.order) + ": texture '" + op.textureKey
							+ "' is not a layer of the terrain tile, skipped there.");
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
				if (op.polygon->Contains(x, z) || op.edge.Nearest(x, z, hit))
				{
					out.waterType = 1;
					out.waterHeight = op.level;
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

	void PreparedTerrain::BuildPlacements(const std::function<float(float, float)>& ground, std::vector<Placement>& out) const
	{
		out.clear();
		std::vector<Placement> section;
		for (const auto& op : m_ops)
		{
			if (op->type != TerrainOpType::SectionPath)
			{
				continue;
			}
			BuildSectionPlacements(op->shape, ground, section);
			for (Placement& p : section)
			{
				if (!Contains(m_region->bounds, p.position.x, p.position.z) || !m_region->polygon->Contains(p.position.x, p.position.z))
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
			Warn("Section placements with duplicate keys were dropped (two section ops of one node share a control).");
			out.erase(duplicate, out.end());
		}
	}

	const String& PreparedTerrain::GetWaterMaterial() const
	{
		return m_waterMaterial;
	}

	const std::vector<String>& PreparedTerrain::GetWarnings() const
	{
		return m_warnings;
	}

	void PreparedTerrain::Warn(const String& message) const
	{
		if (std::find(m_warnings.begin(), m_warnings.end(), message) == m_warnings.end())
		{
			m_warnings.push_back(message);
		}
	}
}
