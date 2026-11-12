// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Sampler nodes. Context dependencies (contextDeps): SampleSpline PointLimit; SampleArea Terrain + PointLimit;
// SampleTerrainSurface Terrain + Region + PointLimit.
// Every sampler stops once it has output ctx.pointLimit points in total (over all items) and reports a
// warning. Each input item yields at most one output Points item carrying the input's tags, item
// attributes and sourceId; items that produce no points are left out; items keep their input order. Items
// of kinds a sampler does not sample are skipped with a warning. Geometry that is non-finite or beyond
// MaxWorldCoordinate is skipped with a warning. How seeds behave for duplicates and overlaps is documented
// once in point_prune.h ("Seeding contract").
//
// SampleSpline  input "In" (Splines, Areas). settings: spacing (arc length between points), offset (signed
//               lateral offset along SampledPath::NormalAtDistance: left of travel for open splines, outward
//               for closed ones), alignToPath (rotate local +Z along the path's direction; otherwise identity),
//               endsOnly (emit only the two end points of open splines; closed splines emit nothing). Points
//               lie at arc distances 0, spacing, 2*spacing, ... (open: up to and including the length; closed:
//               below the length), at the curve's height. Columns: "distance" (Float, arc distance) plus one
//               column per item attribute holding the item's value (an item attribute named "distance" or
//               like a point property, see IsReservedPointName, is dropped with a warning). Seed: Hash(nodeSeed, cellX, cellZ, Hash(Hash(salt, item key), distance
//               in cm)) with the position quantized to 1 cm cells; the item key is the sourceId, or the item's
//               input index when sourceId is 0. Output order: path order (ascending distance) per item.
// SampleArea    input "In" (Areas). settings: cellSize, minSpacing, jitter (0-1), densityAttribute. World-anchored
//               cells (ix, iz) of size cellSize covering the area's bounds; per cell one candidate at
//               ((ix + 0.5 + jitter * (u - 0.5)) * cellSize, y, (iz + 0.5 + jitter * (v - 0.5)) * cellSize) with
//               u, v, a from Hash(nodeSeed, ix, iz, salt) (also the point seed). The candidate is kept if a <
//               density and it lies inside the area (even-odd, as SampledPath::Contains). density is the item
//               attribute named by densityAttribute (Float or Int, clamped to [0,1]; missing or non-numeric:
//               warning, density 1) or 1; it is also stored as the point's density. If minSpacing > 0 the
//               candidates are pruned with PruneByDistance (planar, 1 round), ranked as Self Prune with priority
//               seed ranks them (the lower seed wins a conflict), so the result equals Self Prune (planar,
//               priority seed, same spacing) of the unpruned candidates. The prune runs in bands of cell rows
//               that are extended by its locality radius 3 * minSpacing on both sides, so the result equals a
//               prune of the whole item while memory stays bounded. The point limit counts the points that
//               survive the prune. y is ctx.terrain->Height at the point; without a terrain y = 0, and where the
//               terrain has no height the point is kept at y = 0 with a warning. Output order: by (cellZ, cellX).
//               Besides the point limit, at most 16 * pointLimit + 65536 cells are visited and a band holds at
//               most 4 * pointLimit + 65536 candidates (warning when either is hit).
// SampleTerrainSurface  input "In" (Areas, optional: when the pin has no link the instance region is sampled).
//               settings: cellSize, alignToNormal. One point per world-anchored cell centre inside the area where
//               ctx.terrain has a height, at that height; rotation maps local +Y onto the terrain normal when
//               alignToNormal is set (identity otherwise). Seed Hash(nodeSeed, ix, iz, salt). Without a terrain
//               the node outputs nothing and warns. Output order: by (cellZ, cellX). Same cell budget as
//               SampleArea.

#include "builtin_nodes.h"
#include "evaluator.h"
#include "node_registry.h"
#include "node_util.h"
#include "point_prune.h"

#include "math/radian.h"
#include "pcg/pcg_random.h"

#include <algorithm>
#include <cmath>

namespace mmo::pcg::graph
{
	namespace
	{
		constexpr uint64 SplineSalt = 0x5a3b1e00ull;
		constexpr uint64 AreaSalt = 0xa4ea0001ull;
		constexpr uint64 TerrainSalt = 0x7e44a100ull;
		constexpr float MinCellSize = 0.01f;
		constexpr float MinSpacing = 0.01f;
		constexpr double SplineCellSize = 0.01;

		/// Remaining output points and cells of one node execution.
		struct Budget
		{
			size_t pointsLeft = 0;
			uint64 cellsLeft = 0;

			/// Most candidates held at once (Sample Area keeps one band of candidates before pruning).
			size_t candidateCap = 0;
			bool pointLimitHit = false;
			bool cellLimitHit = false;
			bool candidateLimitHit = false;

			explicit Budget(const size_t pointLimit)
				: pointsLeft(pointLimit)
				, cellsLeft(static_cast<uint64>(pointLimit) * 16u + 65536u)
				, candidateCap(pointLimit * 4u + 65536u)
			{
			}

			bool Exhausted() const
			{
				return pointLimitHit || cellLimitHit || candidateLimitHit;
			}

			void Report(NodeOutput& out, const size_t pointLimit) const
			{
				if (pointLimitHit)
				{
					WarnOnce(out, "point limit of " + std::to_string(pointLimit) + " reached, output truncated");
				}
				if (cellLimitHit)
				{
					WarnOnce(out, "cell budget exhausted (cell size too small for the area), output truncated");
				}
				if (candidateLimitHit)
				{
					WarnOnce(out, "candidate cap reached (cell size too small for the area), output truncated");
				}
			}
		};

		int64 CellIndex(const double v, const double cell)
		{
			return static_cast<int64>(std::floor(v / cell));
		}

		/// Starts an output item that carries the input item's identity.
		DataItem PointsItemFrom(const DataItem& source)
		{
			DataItem item;
			item.kind = ItemKind::Points;
			item.tags = source.tags;
			item.attributes = source.attributes;
			item.sourceId = source.sourceId;
			return item;
		}

		/// Rotation about +Y so that local +Z faces (dx, dz); identity for a zero direction.
		Quaternion YawTowards(const float dx, const float dz)
		{
			if (!(dx * dx + dz * dz > 1.0e-12f))
			{
				return Quaternion::Identity;
			}
			return Quaternion(Radian(std::atan2(dx, dz)), Vector3::UnitY);
		}

		/// Bounds of an item's spline, or invalid (with a warning) if they are unusable.
		bool UsableBounds(NodeOutput& out, const DataItem& item, Bounds2D& bounds)
		{
			bounds = SplineBounds(item.spline);
			if (!bounds.valid)
			{
				return false;
			}
			if (!SaneBounds(bounds))
			{
				WarnOnce(out, "skipped an item with non-finite or out of range geometry");
				return false;
			}
			return true;
		}

		NodeOutput ExecuteSampleSpline(const EvalContext& ctx, const uint64 nodeSeed, const AttributeSet& settings, const std::map<String, Collection>& inputs)
		{
			NodeOutput out;
			Collection& result = out.pins["Out"];
			const auto in = inputs.find("In");
			if (in == inputs.end())
			{
				return out;
			}

			float spacing = SettingFloat(settings, "spacing", 1.0f);
			if (spacing < MinSpacing)
			{
				WarnOnce(out, "spacing below " + std::to_string(MinSpacing) + ", clamped");
				spacing = MinSpacing;
			}
			const float offset = SettingFloat(settings, "offset", 0.0f);
			const bool alignToPath = SettingBool(settings, "alignToPath", true);
			const bool endsOnly = SettingBool(settings, "endsOnly", false);

			Budget budget(ctx.pointLimit);
			for (const DataItem& source : in->second)
			{
				if (budget.Exhausted())
				{
					break;
				}
				if (source.kind != ItemKind::Spline && source.kind != ItemKind::Area)
				{
					WarnOnce(out, "skipped items that are neither splines nor areas");
					continue;
				}
				Bounds2D bounds;
				if (source.spline.points.size() < 2 || !UsableBounds(out, source, bounds))
				{
					continue;
				}

				const SampledPath path(source.spline, EvaluationStep(source.spline, SamplingStep));
				const float length = path.GetLength();
				if (!(length > 0.0f) || !std::isfinite(length))
				{
					continue;
				}

				std::vector<float> distances;
				if (endsOnly)
				{
					if (path.IsClosed())
					{
						WarnOnce(out, "endsOnly: closed splines have no ends and were skipped");
						continue;
					}
					distances = { 0.0f, length };
				}
				else
				{
					const double steps = static_cast<double>(length) / static_cast<double>(spacing);
					const double count = path.IsClosed() ? std::max(1.0, std::ceil(steps - 1.0e-4)) : std::floor(steps + 1.0e-4) + 1.0;
					const size_t wanted = static_cast<size_t>(std::min(count, static_cast<double>(budget.pointsLeft) + 1.0));
					distances.reserve(std::min(wanted, budget.pointsLeft));
					for (size_t k = 0; k < wanted; ++k)
					{
						distances.push_back(std::min(static_cast<float>(static_cast<double>(k) * spacing), length));
					}
				}
				if (distances.size() > budget.pointsLeft)
				{
					distances.resize(budget.pointsLeft);
					budget.pointLimitHit = true;
				}
				budget.pointsLeft -= distances.size();
				if (distances.empty())
				{
					continue;
				}

				// Item key: the producing control, or the input index for generated items (so coincident or
				// derived splines never share seeds); the distance separates self-intersection crossings.
				const size_t itemIndex = static_cast<size_t>(&source - in->second.data());
				const uint64 itemKey = source.sourceId != 0
					? Hash(SplineSalt, static_cast<int64>(source.sourceId), 0)
					: Hash(SplineSalt, static_cast<int64>(itemIndex), 1);
				const float eps = std::min(0.01f, length * 0.001f);

				// Path order: distances ascend.
				DataItem item = PointsItemFrom(source);
				const size_t n = distances.size();
				item.points.points.reserve(n);
				std::vector<AttributeValue> distanceColumn;
				distanceColumn.reserve(n);
				for (const float d : distances)
				{
					const SplineSample s = path.SampleAtDistance(d);
					float nx = 0.0f, nz = 0.0f;
					path.NormalAtDistance(d, nx, nz);

					Point p;
					p.position = Vector3(s.x + nx * offset, s.y, s.z + nz * offset);
					if (alignToPath)
					{
						const SplineSample a = path.SampleAtDistance(d - eps);
						const SplineSample b = path.SampleAtDistance(d + eps);
						p.rotation = YawTowards(b.x - a.x, b.z - a.z);
					}
					const int64 cx = CellIndex(p.position.x, SplineCellSize);
					const int64 cz = CellIndex(p.position.z, SplineCellSize);
					const int64 distanceCm = static_cast<int64>(std::llround(static_cast<double>(d) * 100.0));
					p.seed = Hash(nodeSeed, cx, cz, Hash(itemKey, distanceCm));
					item.points.points.push_back(p);
					distanceColumn.emplace_back(d);
				}
				item.points.columns["distance"] = { AttributeType::Float, std::move(distanceColumn) };
				for (const auto& [name, value] : source.attributes)
				{
					if (name == "distance")
					{
						WarnOnce(out, "item attribute 'distance' is shadowed by the distance column");
						continue;
					}
					if (IsReservedPointName(name))
					{
						WarnOnce(out, "item attributes named like point properties (density, position.*, scale*) were not turned into columns");
						continue;
					}
					item.points.columns[name] = { TypeOf(value), std::vector<AttributeValue>(n, value) };
				}
				result.push_back(std::move(item));
			}
			budget.Report(out, ctx.pointLimit);
			return out;
		}

		float ItemDensity(NodeOutput& out, const DataItem& item, const String& attribute)
		{
			if (attribute.empty())
			{
				return 1.0f;
			}
			const auto it = item.attributes.find(attribute);
			float value = 1.0f;
			if (it == item.attributes.end())
			{
				WarnOnce(out, "density attribute '" + attribute + "' missing on an item, density 1 used");
				return 1.0f;
			}
			if (const float* f = std::get_if<float>(&it->second))
			{
				value = *f;
			}
			else if (const int64* i = std::get_if<int64>(&it->second))
			{
				value = static_cast<float>(*i);
			}
			else
			{
				WarnOnce(out, "density attribute '" + attribute + "' is not numeric on an item, density 1 used");
				return 1.0f;
			}
			if (!std::isfinite(value))
			{
				WarnOnce(out, "density attribute '" + attribute + "' is not finite on an item, density 1 used");
				return 1.0f;
			}
			return std::clamp(value, 0.0f, 1.0f);
		}

		float CellSizeSetting(NodeOutput& out, const AttributeSet& settings)
		{
			float cellSize = SettingFloat(settings, "cellSize", 2.0f);
			if (cellSize < MinCellSize)
			{
				WarnOnce(out, "cellSize below " + std::to_string(MinCellSize) + ", clamped");
				cellSize = MinCellSize;
			}
			return cellSize;
		}

		/// Visits the world-anchored cells covering bounds in (iz, ix) order until the visitor returns false or
		/// the cell budget runs out.
		template <typename Visitor>
		void ForEachCell(const Bounds2D& bounds, const double cell, Budget& budget, Visitor&& visit)
		{
			const int64 ix0 = CellIndex(bounds.minX, cell);
			const int64 ix1 = CellIndex(bounds.maxX, cell);
			const int64 iz0 = CellIndex(bounds.minZ, cell);
			const int64 iz1 = CellIndex(bounds.maxZ, cell);
			for (int64 iz = iz0; iz <= iz1; ++iz)
			{
				for (int64 ix = ix0; ix <= ix1; ++ix)
				{
					if (budget.cellsLeft == 0)
					{
						budget.cellLimitHit = true;
						return;
					}
					--budget.cellsLeft;
					if (!visit(ix, iz))
					{
						return;
					}
				}
			}
		}

		/// Containment index for an area item; empty when the spline does not enclose anything.
		RowIndexedPolygon AreaPolygon(const SampledPath& path, const Bounds2D& bounds, const float cellSize)
		{
			const float rowHeight = std::max(cellSize, (bounds.maxZ - bounds.minZ) / 4096.0f);
			return RowIndexedPolygon(path.IsClosed() ? path.GetSamples() : std::vector<SplineSample>(), rowHeight);
		}

		NodeOutput ExecuteSampleArea(const EvalContext& ctx, const uint64 nodeSeed, const AttributeSet& settings, const std::map<String, Collection>& inputs)
		{
			NodeOutput out;
			Collection& result = out.pins["Out"];
			const auto in = inputs.find("In");
			if (in == inputs.end())
			{
				return out;
			}

			const float cellSize = CellSizeSetting(out, settings);
			const double cell = cellSize;
			const float minSpacing = std::max(0.0f, SettingFloat(settings, "minSpacing", 0.0f));
			const double jitter = std::clamp(SettingFloat(settings, "jitter", 1.0f), 0.0f, 1.0f);
			const String densityAttribute = SettingString(settings, "densityAttribute", "");

			Budget budget(ctx.pointLimit);
			for (const DataItem& source : in->second)
			{
				if (budget.Exhausted())
				{
					break;
				}
				if (source.kind != ItemKind::Area)
				{
					WarnOnce(out, "skipped items that are not areas");
					continue;
				}
				Bounds2D bounds;
				if (source.spline.points.size() < 3 || !UsableBounds(out, source, bounds))
				{
					continue;
				}
				const float density = ItemDensity(out, source, densityAttribute);
				if (!(density > 0.0f))
				{
					// Nothing can be accepted; skipping keeps a zero density from burning the cell budget.
					continue;
				}

				const SampledPath path(source.spline, EvaluationStep(source.spline, cellSize * 0.5f));
				const RowIndexedPolygon polygon = AreaPolygon(path, bounds, cellSize);

				const int64 ix0 = CellIndex(bounds.minX, cell);
				const int64 ix1 = CellIndex(bounds.maxX, cell);
				const int64 iz0 = CellIndex(bounds.minZ, cell);
				const int64 iz1 = CellIndex(bounds.maxZ, cell);

				// Bands of cell rows. With a prune, each band is generated with extra rows on both sides that
				// cover the prune's locality radius (3 * minSpacing for one round), so the points of the band's
				// own rows come out exactly as from a prune of the whole item.
				PruneOptions options;
				options.minSpacing = minSpacing;
				options.planar = true;
				options.rounds = 1;
				const bool prune = minSpacing > 0.0f;
				const double radius = (2.0 * options.rounds + 1.0) * minSpacing;
				const int64 marginRows = prune ? static_cast<int64>(std::ceil(radius / cell)) + 1 : 0;
				const int64 bandRows = std::max<int64>(16, 4 * marginRows);

				DataItem item = PointsItemFrom(source);
				std::vector<Point>& points = item.points.points;
				std::vector<Point> candidates;
				std::vector<uint8> ownRow;
				for (int64 band = iz0; band <= iz1 && !budget.Exhausted(); band += bandRows)
				{
					const int64 bandEnd = std::min(iz1, band + bandRows - 1);
					const int64 genBegin = std::max(iz0, band - marginRows);
					const int64 genEnd = std::min(iz1, bandEnd + marginRows);
					candidates.clear();
					ownRow.clear();
					for (int64 iz = genBegin; iz <= genEnd && !budget.Exhausted(); ++iz)
					{
						for (int64 ix = ix0; ix <= ix1; ++ix)
						{
							if (budget.cellsLeft == 0)
							{
								budget.cellLimitHit = true;
								break;
							}
							--budget.cellsLeft;

							const uint64 h = Hash(nodeSeed, ix, iz, AreaSalt);
							if (!(HashToUnit(Hash(h, 3)) < density))
							{
								continue;
							}
							const float x = static_cast<float>((static_cast<double>(ix) + 0.5 + jitter * (HashToUnit(Hash(h, 1)) - 0.5)) * cell);
							const float z = static_cast<float>((static_cast<double>(iz) + 0.5 + jitter * (HashToUnit(Hash(h, 2)) - 0.5)) * cell);
							if (!polygon.Contains(x, z))
							{
								continue;
							}
							if (candidates.size() >= budget.candidateCap)
							{
								budget.candidateLimitHit = true;
								break;
							}
							Point p;
							p.position = Vector3(x, 0.0f, z);
							p.density = density;
							p.seed = ~h;   // inverted until the prune: PruneByDistance keeps the higher seed, the node the lower
							candidates.push_back(p);
							ownRow.push_back(iz >= band && iz <= bandEnd ? 1 : 0);
						}
					}

					std::vector<uint8> keep = prune ? PruneByDistance(candidates, options) : std::vector<uint8>(candidates.size(), 1);
					for (size_t i = 0; i < candidates.size(); ++i)
					{
						if (keep[i] == 0 || ownRow[i] == 0)
						{
							continue;
						}
						if (budget.pointsLeft == 0)
						{
							budget.pointLimitHit = true;
							break;
						}
						--budget.pointsLeft;
						points.push_back(candidates[i]);
						points.back().seed = ~points.back().seed;
					}
				}

				if (ctx.terrain != nullptr)
				{
					for (Point& p : points)
					{
						float h = 0.0f;
						if (ctx.terrain->Height(p.position.x, p.position.z, h) && std::isfinite(h))
						{
							p.position.y = h;
						}
						else
						{
							WarnOnce(out, "some points have no terrain height and were kept at y 0");
						}
					}
				}
				if (!points.empty())
				{
					result.push_back(std::move(item));
				}
			}
			budget.Report(out, ctx.pointLimit);
			return out;
		}

		NodeOutput ExecuteSampleTerrainSurface(const EvalContext& ctx, const uint64 nodeSeed, const AttributeSet& settings, const std::map<String, Collection>& inputs)
		{
			NodeOutput out;
			Collection& result = out.pins["Out"];
			if (ctx.terrain == nullptr)
			{
				WarnOnce(out, "no terrain available");
				return out;
			}

			Collection regionItems;
			const Collection* areas = nullptr;
			const auto in = inputs.find("In");
			if (in != inputs.end())
			{
				areas = &in->second;
			}
			else
			{
				if (ctx.region.points.size() >= 3)
				{
					DataItem region;
					region.kind = ItemKind::Area;
					region.spline = ctx.region;
					region.spline.closed = true;
					regionItems.push_back(std::move(region));
				}
				areas = &regionItems;
			}

			const float cellSize = CellSizeSetting(out, settings);
			const double cell = cellSize;
			const bool alignToNormal = SettingBool(settings, "alignToNormal", false);

			Budget budget(ctx.pointLimit);
			for (const DataItem& source : *areas)
			{
				if (budget.Exhausted())
				{
					break;
				}
				if (source.kind != ItemKind::Area)
				{
					WarnOnce(out, "skipped items that are not areas");
					continue;
				}
				Bounds2D bounds;
				if (source.spline.points.size() < 3 || !UsableBounds(out, source, bounds))
				{
					continue;
				}

				const SampledPath path(source.spline, EvaluationStep(source.spline, cellSize * 0.5f));
				const RowIndexedPolygon polygon = AreaPolygon(path, bounds, cellSize);

				DataItem item = PointsItemFrom(source);
				std::vector<Point>& points = item.points.points;
				ForEachCell(bounds, cell, budget, [&](const int64 ix, const int64 iz)
				{
					const float x = static_cast<float>((static_cast<double>(ix) + 0.5) * cell);
					const float z = static_cast<float>((static_cast<double>(iz) + 0.5) * cell);
					float h = 0.0f;
					if (!polygon.Contains(x, z) || !ctx.terrain->Height(x, z, h) || !std::isfinite(h))
					{
						return true;
					}
					if (budget.pointsLeft == 0)
					{
						budget.pointLimitHit = true;
						return false;
					}
					--budget.pointsLeft;
					Point p;
					p.position = Vector3(x, h, z);
					if (alignToNormal)
					{
						p.rotation = RotationOntoNormal(ctx.terrain->Normal(x, z));
					}
					p.seed = Hash(nodeSeed, ix, iz, TerrainSalt);
					points.push_back(p);
					return true;
				});

				if (!points.empty())
				{
					result.push_back(std::move(item));
				}
			}
			budget.Report(out, ctx.pointLimit);
			return out;
		}
	}

	void RegisterSamplerNodes(NodeRegistry& registry)
	{
		{
			NodeTypeDef def;
			def.type = "SampleSpline";
			def.category = "Sampler";
			def.title = "Sample Spline";
			def.inputs.push_back(PinDef{ "In", { ItemKind::Spline, ItemKind::Area }, true, true });
			def.outputs.push_back(PinDef{ "Out", { ItemKind::Points } });
			def.settings.push_back(SettingDef{ "spacing", 1.0f, 0.01f, 100.0f, {}, "Arc length between points" });
			def.settings.push_back(SettingDef{ "offset", 0.0f, -50.0f, 50.0f, {}, "Signed lateral offset (left of travel / outward for loops)" });
			def.settings.push_back(SettingDef{ "alignToPath", true, 0, 0, {}, "Rotate points to face along the path" });
			def.settings.push_back(SettingDef{ "endsOnly", false, 0, 0, {}, "Emit only the two end points of open splines" });
			def.contextDeps = context_dep::PointLimit;
			def.execute = ExecuteSampleSpline;
			registry.Register(std::move(def));
		}
		{
			NodeTypeDef def;
			def.type = "SampleArea";
			def.category = "Sampler";
			def.title = "Sample Area";
			def.inputs.push_back(PinDef{ "In", { ItemKind::Area }, true, true });
			def.outputs.push_back(PinDef{ "Out", { ItemKind::Points } });
			def.settings.push_back(SettingDef{ "cellSize", 2.0f, 0.01f, 100.0f, {}, "Size of the world-anchored candidate cells" });
			def.settings.push_back(SettingDef{ "minSpacing", 0.0f, 0.0f, 100.0f, {}, "Minimum distance between points (0 = off)" });
			def.settings.push_back(SettingDef{ "jitter", 1.0f, 0.0f, 1.0f, {}, "Random offset of a candidate inside its cell" });
			def.settings.push_back(SettingDef{ "densityAttribute", String(), 0, 0, {}, "Item attribute (0-1) scaling the acceptance (empty = 1)" });
			def.contextDeps = context_dep::Terrain | context_dep::PointLimit;
			def.execute = ExecuteSampleArea;
			registry.Register(std::move(def));
		}
		{
			NodeTypeDef def;
			def.type = "SampleTerrainSurface";
			def.category = "Sampler";
			def.title = "Sample Terrain Surface";
			def.inputs.push_back(PinDef{ "In", { ItemKind::Area }, true, false });
			def.outputs.push_back(PinDef{ "Out", { ItemKind::Points } });
			def.settings.push_back(SettingDef{ "cellSize", 2.0f, 0.01f, 100.0f, {}, "Distance between points" });
			def.settings.push_back(SettingDef{ "alignToNormal", false, 0, 0, {}, "Rotate points onto the terrain normal" });
			def.contextDeps = context_dep::Terrain | context_dep::Region | context_dep::PointLimit;
			def.execute = ExecuteSampleTerrainSurface;
			registry.Register(std::move(def));
		}
	}
}
