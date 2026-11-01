// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "terrain_compose.h"

#include "node_util.h"
#include "polyline_index.h"

#include "pcg/pcg_random.h"
#include "pcg/section_path.h"

#include <algorithm>
#include <cmath>
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

		// WaterArea.
		float level = 0.0f;
		float margin = 0.0f;

		// SectionPath.
		SectionPathShape shape;
		bool remapLayers = false;
		/// Section asset path (for warnings).
		String sectionAsset;
		/// Section layers the section paints somewhere (variant paint with coverage, or the mass paint layer).
		std::array<bool, 4> paintedLayers{};
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
				in.itemIndex = static_cast<uint32>(i);
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
					if (!SectionParamText(value, text))
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
						ids.push_back(Hash(in.sourceId, static_cast<int64>(in.itemIndex), static_cast<int64>(i), 0x5ec7));
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
			else
			{
				samples = &op->edge.GetSamples();
				outline.closed = op->edge.IsClosed();
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
				if (!wantSplat)
				{
					break;
				}
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
				if (wantWater && (op.polygon->Contains(x, z) || op.edge.Nearest(x, z, hit)))
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
					+ "'): section paint with texture(s) " + textures + " skipped on tiles that have no layer with that texture.");
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
			BuildSectionPlacements(op->shape, ground, section);
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
