// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Filter and spatial nodes. Shared rules: items keep their input order and points keep their order inside an
// item. A node that splits points writes the kept points to "Out" and the others to "Rejected", each as a copy
// of the input item (tags, item attributes, sourceId, all columns) holding only its points; copies without
// points are left out. Items of a kind a node does not process are skipped with a warning. Names of point
// properties (density, position.x|y|z, scale, scale.x|y|z) always mean the property (see IsReservedPointName).
// Area and Spline inputs are prepared by PrepareControl (node_util.h): geometry that is non-finite or out of
// range, and Areas whose spline is not closed, are skipped with a warning.
//
// FilterByTag   input "In" (any kind). settings: tags (comma list), matchAny (false: the item needs all tags;
//               true: any one of them), excludeTags (comma list; an item with any of them is rejected). No tags
//               means every item matches. Whole items go to "Out" or "Rejected".
// FilterByAttribute  input "In" (any kind). settings: attribute, op (< <= == != >= > inRange), value, valueMax
//               (inRange: value <= x <= valueMax), stringValue (compared by == and != on String attributes),
//               scope (auto | item). auto: points of a Points item are tested one by one (point property, else
//               column, else the item attribute; see ReadPointValue); other items by their item attribute. item:
//               every item is tested by its item attribute and routed whole. Float, Int and Bool compare as
//               numbers. Rejected with one warning per node: a missing attribute, a non-finite number, an ordering
//               op on a String, any Vector3.
// FilterByTerrain  input "In" (Points), reads ctx.terrain. settings: minSlope, maxSlope (degrees between the
//               terrain normal and up), minHeight, maxHeight (terrain height at the point, not the point's y),
//               splatKey (empty = off) with minSplatWeight, excludeUnderWater (water level above the terrain).
//               All ranges are inclusive. Points where the terrain has no height are rejected with a warning;
//               without a terrain every point is rejected with a warning.
// DensityFilter input "In" (Points). settings: minDensity, maxDensity; keeps points with density in [min, max].
// SelfPrune     input "In" (Points, all items pruned together). settings: minSpacing, priority (density | seed),
//               planar. Uses PruneByDistance (1 round, grid cell = minSpacing, locality radius 3 * minSpacing):
//               priority density ranks higher density first, then the lower seed, then the earlier point (in item
//               order, so of two duplicated items the first wins); priority seed skips the density step.
// Difference    inputs "In" (Points), "Subtract" (Areas, Splines; optional). settings: distance,
//               clearanceAttribute (default "clearance"). Removes points inside any Area (even-odd, as the
//               samplers) and points whose planar distance to any Spline is at most distance + the spline's
//               clearance (Float or Int item attribute; missing = 0; other types warn and count as 0). A spline
//               whose total radius is not positive removes nothing. Area minus Area is phase 3.
// Intersection  inputs "In" (Points), "Areas" (Areas). Keeps points inside any area.
// DistanceTo    inputs "In" (Points), "Target" (Areas, Splines; optional). settings: attributeName (default
//               "distanceTo"), maxDistance. Writes per point the signed planar distance to the nearest target:
//               splines give the distance to the curve, areas the distance to their edge, negative inside. With
//               several targets the minimum wins (inside an area always beats outside). Values are clamped to
//               [-maxDistance, maxDistance]; with no target every point gets maxDistance. The value goes into a
//               Float column (a column of that name with another type is replaced, with a warning), or into the
//               point property for a reserved name.

#include "builtin_nodes.h"
#include "evaluator.h"
#include "node_registry.h"
#include "node_util.h"
#include "point_prune.h"

#include "math/constants.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace mmo::pcg::graph
{
	namespace
	{
		/// Writes the kept and rejected points of an item to the two output collections.
		void SplitPoints(const DataItem& item, const std::vector<uint8>& keep, Collection& kept, Collection& rejected)
		{
			std::vector<uint8> inverse(keep.size());
			size_t keptCount = 0;
			for (size_t i = 0; i < keep.size(); ++i)
			{
				inverse[i] = keep[i] != 0 ? 0 : 1;
				keptCount += keep[i] != 0 ? 1 : 0;
			}
			if (keptCount > 0)
			{
				DataItem copy = item;
				copy.points.Keep(keep);
				kept.push_back(std::move(copy));
			}
			if (keptCount < keep.size())
			{
				DataItem copy = item;
				copy.points.Keep(inverse);
				rejected.push_back(std::move(copy));
			}
		}

		/// Applies a per-point predicate to every Points item of "In".
		template <typename Predicate>
		NodeOutput FilterPoints(const std::map<String, Collection>& inputs, NodeOutput out, Predicate&& keepPoint)
		{
			Collection& kept = out.pins["Out"];
			Collection& rejected = out.pins["Rejected"];
			const Collection* in = FindInput(inputs, "In");
			if (in == nullptr)
			{
				return out;
			}
			for (const DataItem& item : *in)
			{
				if (item.kind != ItemKind::Points)
				{
					WarnOnce(out, "skipped items that are not points");
					continue;
				}
				std::vector<uint8> keep(item.points.points.size());
				for (size_t i = 0; i < keep.size(); ++i)
				{
					keep[i] = keepPoint(out, item, i) ? 1 : 0;
				}
				SplitPoints(item, keep, kept, rejected);
			}
			return out;
		}

		bool HasAllTags(const std::set<String>& tags, const std::vector<String>& wanted)
		{
			return std::all_of(wanted.begin(), wanted.end(), [&](const String& t) { return tags.count(t) != 0; });
		}

		bool HasAnyTag(const std::set<String>& tags, const std::vector<String>& wanted)
		{
			return std::any_of(wanted.begin(), wanted.end(), [&](const String& t) { return tags.count(t) != 0; });
		}

		NodeOutput ExecuteFilterByTag(const EvalContext&, uint64, const AttributeSet& settings, const std::map<String, Collection>& inputs)
		{
			NodeOutput out;
			Collection& kept = out.pins["Out"];
			Collection& rejected = out.pins["Rejected"];
			const Collection* in = FindInput(inputs, "In");
			if (in == nullptr)
			{
				return out;
			}
			const std::vector<String> tags = SplitList(SettingString(settings, "tags", ""));
			const std::vector<String> exclude = SplitList(SettingString(settings, "excludeTags", ""));
			const bool matchAny = SettingBool(settings, "matchAny", false);
			for (const DataItem& item : *in)
			{
				bool pass = tags.empty() || (matchAny ? HasAnyTag(item.tags, tags) : HasAllTags(item.tags, tags));
				pass = pass && !HasAnyTag(item.tags, exclude);
				(pass ? kept : rejected).push_back(item);
			}
			return out;
		}

		enum class CompareOp
		{
			Less,
			LessEqual,
			Equal,
			NotEqual,
			GreaterEqual,
			Greater,
			InRange,
			Invalid
		};

		CompareOp ParseOp(const String& op)
		{
			static const std::pair<const char*, CompareOp> ops[] = {
				{ "<", CompareOp::Less }, { "<=", CompareOp::LessEqual }, { "==", CompareOp::Equal }, { "!=", CompareOp::NotEqual },
				{ ">=", CompareOp::GreaterEqual }, { ">", CompareOp::Greater }, { "inRange", CompareOp::InRange }
			};
			for (const auto& [name, value] : ops)
			{
				if (op == name)
				{
					return value;
				}
			}
			return CompareOp::Invalid;
		}

		/// Settings of Filter by Attribute.
		struct AttributeRule
		{
			String attribute;
			CompareOp op = CompareOp::LessEqual;
			float value = 0.0f;
			float valueMax = 0.0f;
			String stringValue;

			bool Passes(NodeOutput& out, const AttributeValue& v) const
			{
				if (const String* s = std::get_if<String>(&v))
				{
					if (op == CompareOp::Equal)
					{
						return *s == stringValue;
					}
					if (op == CompareOp::NotEqual)
					{
						return *s != stringValue;
					}
					WarnOnce(out, "String attribute '" + attribute + "' supports only == and !=; rejected");
					return false;
				}
				float x = 0.0f;
				if (!ToNumber(v, x))
				{
					WarnOnce(out, "attribute '" + attribute + "' is not a finite number or string; rejected");
					return false;
				}
				switch (op)
				{
				case CompareOp::Less:
					return x < value;
				case CompareOp::LessEqual:
					return x <= value;
				case CompareOp::Equal:
					return x == value;
				case CompareOp::NotEqual:
					return x != value;
				case CompareOp::GreaterEqual:
					return x >= value;
				case CompareOp::Greater:
					return x > value;
				case CompareOp::InRange:
					return x >= value && x <= valueMax;
				default:
					return false;
				}
			}

			void WarnMissing(NodeOutput& out) const
			{
				WarnOnce(out, "attribute '" + attribute + "' missing; rejected");
			}
		};

		NodeOutput ExecuteFilterByAttribute(const EvalContext&, uint64, const AttributeSet& settings, const std::map<String, Collection>& inputs)
		{
			NodeOutput out;
			Collection& kept = out.pins["Out"];
			Collection& rejected = out.pins["Rejected"];
			const Collection* in = FindInput(inputs, "In");
			if (in == nullptr)
			{
				return out;
			}

			AttributeRule rule;
			rule.attribute = SettingString(settings, "attribute", "");
			rule.op = ParseOp(SettingString(settings, "op", "<="));
			rule.value = SettingFloat(settings, "value", 0.0f);
			rule.valueMax = SettingFloat(settings, "valueMax", 0.0f);
			rule.stringValue = SettingString(settings, "stringValue", "");
			if (rule.op == CompareOp::Invalid)
			{
				out.error = "unknown op '" + SettingString(settings, "op", "") + "'";
				return out;
			}
			const String scope = SettingString(settings, "scope", "auto");
			if (scope != "auto" && scope != "item")
			{
				out.error = "unknown scope '" + scope + "'";
				return out;
			}
			const bool itemScope = scope == "item";

			for (const DataItem& item : *in)
			{
				if (itemScope || item.kind != ItemKind::Points)
				{
					const auto it = item.attributes.find(rule.attribute);
					bool pass = false;
					if (it == item.attributes.end())
					{
						rule.WarnMissing(out);
					}
					else
					{
						pass = rule.Passes(out, it->second);
					}
					(pass ? kept : rejected).push_back(item);
					continue;
				}

				std::vector<uint8> keep(item.points.points.size());
				for (size_t i = 0; i < keep.size(); ++i)
				{
					AttributeValue v;
					if (!ReadPointValue(item, i, rule.attribute, v))
					{
						rule.WarnMissing(out);
						continue;
					}
					keep[i] = rule.Passes(out, v) ? 1 : 0;
				}
				SplitPoints(item, keep, kept, rejected);
			}
			return out;
		}

		NodeOutput ExecuteFilterByTerrain(const EvalContext& ctx, uint64, const AttributeSet& settings, const std::map<String, Collection>& inputs)
		{
			const float minSlope = SettingFloat(settings, "minSlope", 0.0f);
			const float maxSlope = SettingFloat(settings, "maxSlope", 90.0f);
			const float minHeight = SettingFloat(settings, "minHeight", -1.0e6f);
			const float maxHeight = SettingFloat(settings, "maxHeight", 1.0e6f);
			const String splatKey = SettingString(settings, "splatKey", "");
			const float minSplatWeight = SettingFloat(settings, "minSplatWeight", 0.5f);
			const bool excludeUnderWater = SettingBool(settings, "excludeUnderWater", false);
			const ITerrainSampler* terrain = ctx.terrain;

			return FilterPoints(inputs, NodeOutput(), [&](NodeOutput& out, const DataItem& item, const size_t i)
			{
				if (terrain == nullptr)
				{
					WarnOnce(out, "no terrain available; all points rejected");
					return false;
				}
				const Vector3& pos = item.points.points[i].position;
				float height = 0.0f;
				if (!terrain->Height(pos.x, pos.z, height) || !std::isfinite(height))
				{
					WarnOnce(out, "points without terrain were rejected");
					return false;
				}
				if (height < minHeight || height > maxHeight)
				{
					return false;
				}
				const Vector3 normal = terrain->Normal(pos.x, pos.z);
				const float len = normal.GetLength();
				if (!(len > 1.0e-6f) || !std::isfinite(len))
				{
					WarnOnce(out, "points with an invalid terrain normal were rejected");
					return false;
				}
				const float slope = std::acos(std::clamp(normal.y / len, -1.0f, 1.0f)) * 180.0f / Pi;
				if (slope < minSlope || slope > maxSlope)
				{
					return false;
				}
				if (!splatKey.empty() && !(terrain->SplatWeight(splatKey, pos.x, pos.z) >= minSplatWeight))
				{
					return false;
				}
				float level = 0.0f;
				if (excludeUnderWater && terrain->WaterLevel(pos.x, pos.z, level) && level > height)
				{
					return false;
				}
				return true;
			});
		}

		NodeOutput ExecuteDensityFilter(const EvalContext&, uint64, const AttributeSet& settings, const std::map<String, Collection>& inputs)
		{
			const float minDensity = SettingFloat(settings, "minDensity", 0.0f);
			const float maxDensity = SettingFloat(settings, "maxDensity", 1.0f);
			return FilterPoints(inputs, NodeOutput(), [&](NodeOutput&, const DataItem& item, const size_t i)
			{
				const float d = item.points.points[i].density;
				return d >= minDensity && d <= maxDensity;
			});
		}

		NodeOutput ExecuteSelfPrune(const EvalContext&, uint64, const AttributeSet& settings, const std::map<String, Collection>& inputs)
		{
			NodeOutput out;
			Collection& kept = out.pins["Out"];
			Collection& rejected = out.pins["Rejected"];
			const Collection* in = FindInput(inputs, "In");
			if (in == nullptr)
			{
				return out;
			}

			PruneOptions options;
			options.minSpacing = std::max(0.0f, SettingFloat(settings, "minSpacing", 2.0f));
			options.planar = SettingBool(settings, "planar", true);
			options.rounds = 1;
			const String priority = SettingString(settings, "priority", "density");
			if (priority != "density" && priority != "seed")
			{
				out.error = "unknown priority '" + priority + "'";
				return out;
			}
			const bool byDensity = priority == "density";

			// All items are pruned together. PruneByDistance ranks the higher seed first; the node ranks the lower
			// seed first, so the seeds are inverted for the prune only.
			std::vector<Point> points;
			std::vector<float> priorities;
			for (const DataItem& item : *in)
			{
				if (item.kind != ItemKind::Points)
				{
					WarnOnce(out, "skipped items that are not points");
					continue;
				}
				for (const Point& p : item.points.points)
				{
					Point ranked = p;
					ranked.seed = ~p.seed;
					points.push_back(ranked);
					priorities.push_back(std::isfinite(p.density) ? p.density : 0.0f);
				}
			}
			const std::vector<uint8> keep = PruneByDistance(points, options, byDensity ? &priorities : nullptr);

			size_t offset = 0;
			for (const DataItem& item : *in)
			{
				if (item.kind != ItemKind::Points)
				{
					continue;
				}
				const size_t n = item.points.points.size();
				const std::vector<uint8> itemKeep(keep.begin() + static_cast<std::ptrdiff_t>(offset), keep.begin() + static_cast<std::ptrdiff_t>(offset + n));
				offset += n;
				SplitPoints(item, itemKeep, kept, rejected);
			}
			return out;
		}

		NodeOutput ExecuteDifference(const EvalContext&, uint64, const AttributeSet& settings, const std::map<String, Collection>& inputs)
		{
			NodeOutput out;
			const float distance = SettingFloat(settings, "distance", 0.0f);
			const String clearanceAttribute = SettingString(settings, "clearanceAttribute", "clearance");

			std::vector<PreparedControl> areas;
			std::vector<SplineTarget> splines;
			if (const Collection* subtract = FindInput(inputs, "Subtract"))
			{
				for (const DataItem& item : *subtract)
				{
					if (item.kind == ItemKind::Area)
					{
						PreparedControl target;
						if (PrepareControl(out, item, target))
						{
							areas.push_back(std::move(target));
						}
					}
					else if (item.kind == ItemKind::Spline)
					{
						const float radius = distance + ItemClearance(out, item, clearanceAttribute);
						SplineTarget target;
						if (radius > 0.0f && std::isfinite(radius) && PrepareControl(out, item, target.control))
						{
							target.radius = radius;
							splines.push_back(std::move(target));
						}
					}
					else
					{
						WarnOnce(out, "skipped subtract items that are neither areas nor splines");
					}
				}
			}

			return FilterPoints(inputs, std::move(out), [&](NodeOutput&, const DataItem& item, const size_t i)
			{
				const Vector3& pos = item.points.points[i].position;
				if (InsideAny(areas, pos.x, pos.z))
				{
					return false;
				}
				for (const SplineTarget& spline : splines)
				{
					float d = 0.0f;
					if (spline.control.curve->Nearest(pos.x, pos.z, spline.radius, d))
					{
						return false;
					}
				}
				return true;
			});
		}

		NodeOutput ExecuteIntersection(const EvalContext&, uint64, const AttributeSet&, const std::map<String, Collection>& inputs)
		{
			NodeOutput out;
			std::vector<PreparedControl> areas;
			if (const Collection* areaItems = FindInput(inputs, "Areas"))
			{
				for (const DataItem& item : *areaItems)
				{
					PreparedControl target;
					if (item.kind != ItemKind::Area)
					{
						WarnOnce(out, "skipped area items that are not areas");
					}
					else if (PrepareControl(out, item, target))
					{
						areas.push_back(std::move(target));
					}
				}
			}
			return FilterPoints(inputs, std::move(out), [&](NodeOutput&, const DataItem& item, const size_t i)
			{
				const Vector3& pos = item.points.points[i].position;
				return InsideAny(areas, pos.x, pos.z);
			});
		}

		NodeOutput ExecuteDistanceTo(const EvalContext&, uint64, const AttributeSet& settings, const std::map<String, Collection>& inputs)
		{
			NodeOutput out;
			Collection& result = out.pins["Out"];
			const Collection* in = FindInput(inputs, "In");
			if (in == nullptr)
			{
				return out;
			}
			const String name = SettingString(settings, "attributeName", "distanceTo");
			if (name.empty())
			{
				out.error = "attributeName is empty";
				return out;
			}
			const float maxDistance = std::max(0.0f, SettingFloat(settings, "maxDistance", 1000.0f));

			std::vector<PreparedControl> areas;
			std::vector<SplineTarget> splines;
			if (const Collection* targets = FindInput(inputs, "Target"))
			{
				for (const DataItem& item : *targets)
				{
					if (item.kind == ItemKind::Area)
					{
						PreparedControl target;
						if (PrepareControl(out, item, target))
						{
							areas.push_back(std::move(target));
						}
					}
					else if (item.kind == ItemKind::Spline)
					{
						SplineTarget target;
						if (PrepareControl(out, item, target.control))
						{
							splines.push_back(std::move(target));
						}
					}
					else
					{
						WarnOnce(out, "skipped target items that are neither areas nor splines");
					}
				}
			}

			for (const DataItem& source : *in)
			{
				if (source.kind != ItemKind::Points)
				{
					WarnOnce(out, "skipped items that are not points");
					continue;
				}
				if (source.points.points.empty())
				{
					continue;
				}
				DataItem item = source;
				const auto column = item.points.columns.find(name);
				if (!IsReservedPointName(name) && column != item.points.columns.end() && column->second.first != AttributeType::Float)
				{
					WarnOnce(out, "column '" + name + "' had another type and was replaced");
					item.points.columns.erase(column);
				}
				for (size_t i = 0; i < item.points.points.size(); ++i)
				{
					const Vector3& pos = item.points.points[i].position;
					float value = maxDistance;
					for (const SplineTarget& spline : splines)
					{
						float d = 0.0f;
						if (spline.control.curve->Nearest(pos.x, pos.z, maxDistance, d))
						{
							value = std::min(value, d);
						}
					}
					for (const PreparedControl& area : areas)
					{
						float d = maxDistance;
						area.curve->Nearest(pos.x, pos.z, maxDistance, d);
						value = std::min(value, area.polygon->Contains(pos.x, pos.z) ? -d : d);
					}
					WritePointNumber(item, i, name, std::clamp(value, -maxDistance, maxDistance));
				}
				result.push_back(std::move(item));
			}
			return out;
		}

		void AddSplitOutputs(NodeTypeDef& def, const std::vector<ItemKind>& kinds)
		{
			def.outputs.push_back(PinDef{ "Out", kinds });
			def.outputs.push_back(PinDef{ "Rejected", kinds });
		}
	}

	void RegisterFilterNodes(NodeRegistry& registry)
	{
		{
			NodeTypeDef def;
			def.type = "FilterByTag";
			def.category = "Filter";
			def.title = "Filter by Tag";
			def.inputs.push_back(PinDef{ "In", {}, true, true });
			AddSplitOutputs(def, {});
			def.settings.push_back(SettingDef{ "tags", String(), 0, 0, {}, "Comma list of tags an item must have" });
			def.settings.push_back(SettingDef{ "matchAny", false, 0, 0, {}, "Any one of the tags is enough (instead of all)" });
			def.settings.push_back(SettingDef{ "excludeTags", String(), 0, 0, {}, "Comma list of tags that reject an item" });
			def.execute = ExecuteFilterByTag;
			registry.Register(std::move(def));
		}
		{
			NodeTypeDef def;
			def.type = "FilterByAttribute";
			def.category = "Filter";
			def.title = "Filter by Attribute";
			def.inputs.push_back(PinDef{ "In", {}, true, true });
			AddSplitOutputs(def, {});
			def.settings.push_back(SettingDef{ "attribute", String(), 0, 0, {}, "Point or item attribute (or density, position.y, ...)" });
			def.settings.push_back(SettingDef{ "op", String("<="), 0, 0, { "<", "<=", "==", "!=", ">=", ">", "inRange" }, "Comparison" });
			def.settings.push_back(SettingDef{ "value", 0.0f, 0, 0, {}, "Number to compare with (inRange: lower bound)" });
			def.settings.push_back(SettingDef{ "valueMax", 0.0f, 0, 0, {}, "Upper bound for inRange" });
			def.settings.push_back(SettingDef{ "stringValue", String(), 0, 0, {}, "Text to compare String attributes with (== and != only)" });
			def.settings.push_back(SettingDef{ "scope", String("auto"), 0, 0, { "auto", "item" }, "auto: per point for points; item: whole items" });
			def.execute = ExecuteFilterByAttribute;
			registry.Register(std::move(def));
		}
		{
			NodeTypeDef def;
			def.type = "FilterByTerrain";
			def.category = "Filter";
			def.title = "Filter by Terrain";
			def.inputs.push_back(PointsInputPin());
			AddSplitOutputs(def, { ItemKind::Points });
			def.settings.push_back(SettingDef{ "minSlope", 0.0f, 0.0f, 90.0f, {}, "Minimum slope in degrees" });
			def.settings.push_back(SettingDef{ "maxSlope", 90.0f, 0.0f, 90.0f, {}, "Maximum slope in degrees" });
			def.settings.push_back(SettingDef{ "minHeight", -1.0e6f, 0, 0, {}, "Minimum terrain height" });
			def.settings.push_back(SettingDef{ "maxHeight", 1.0e6f, 0, 0, {}, "Maximum terrain height" });
			def.settings.push_back(SettingDef{ "splatKey", String(), 0, 0, {}, "Terrain texture that must be present (empty = off)" });
			def.settings.push_back(SettingDef{ "minSplatWeight", 0.5f, 0.0f, 1.0f, {}, "Minimum blend weight of splatKey" });
			def.settings.push_back(SettingDef{ "excludeUnderWater", false, 0, 0, {}, "Reject points below the water level" });
			def.contextDeps = context_dep::Terrain;
			def.execute = ExecuteFilterByTerrain;
			registry.Register(std::move(def));
		}
		{
			NodeTypeDef def;
			def.type = "DensityFilter";
			def.category = "Filter";
			def.title = "Density Filter";
			def.inputs.push_back(PointsInputPin());
			AddSplitOutputs(def, { ItemKind::Points });
			def.settings.push_back(SettingDef{ "minDensity", 0.0f, 0.0f, 1.0f, {}, "Lowest density kept" });
			def.settings.push_back(SettingDef{ "maxDensity", 1.0f, 0.0f, 1.0f, {}, "Highest density kept" });
			def.execute = ExecuteDensityFilter;
			registry.Register(std::move(def));
		}
		{
			NodeTypeDef def;
			def.type = "SelfPrune";
			def.category = "Filter";
			def.title = "Self Prune";
			def.inputs.push_back(PointsInputPin());
			AddSplitOutputs(def, { ItemKind::Points });
			def.settings.push_back(SettingDef{ "minSpacing", 2.0f, 0.0f, 100.0f, {}, "Minimum distance between kept points" });
			def.settings.push_back(SettingDef{ "priority", String("density"), 0, 0, { "density", "seed" }, "Which point survives a conflict" });
			def.settings.push_back(SettingDef{ "planar", true, 0, 0, {}, "Measure distances on the x/z plane" });
			def.execute = ExecuteSelfPrune;
			registry.Register(std::move(def));
		}
		{
			NodeTypeDef def;
			def.type = "Difference";
			def.category = "Spatial";
			def.title = "Difference";
			def.inputs.push_back(PointsInputPin());
			def.inputs.push_back(PinDef{ "Subtract", { ItemKind::Area, ItemKind::Spline }, true, false });
			AddSplitOutputs(def, { ItemKind::Points });
			def.settings.push_back(SettingDef{ "distance", 0.0f, 0.0f, 100.0f, {}, "Removal distance around splines" });
			def.settings.push_back(SettingDef{ "clearanceAttribute", String("clearance"), 0, 0, {}, "Spline item attribute added to distance (empty = off)" });
			def.execute = ExecuteDifference;
			registry.Register(std::move(def));
		}
		{
			NodeTypeDef def;
			def.type = "Intersection";
			def.category = "Spatial";
			def.title = "Intersection";
			def.inputs.push_back(PointsInputPin());
			def.inputs.push_back(PinDef{ "Areas", { ItemKind::Area }, true, true });
			AddSplitOutputs(def, { ItemKind::Points });
			def.execute = ExecuteIntersection;
			registry.Register(std::move(def));
		}
		{
			NodeTypeDef def;
			def.type = "DistanceTo";
			def.category = "Spatial";
			def.title = "Distance To";
			def.inputs.push_back(PointsInputPin());
			def.inputs.push_back(PinDef{ "Target", { ItemKind::Area, ItemKind::Spline }, true, false });
			def.outputs.push_back(PinDef{ "Out", { ItemKind::Points } });
			def.settings.push_back(SettingDef{ "attributeName", String("distanceTo"), 0, 0, {}, "Attribute receiving the signed distance" });
			def.settings.push_back(SettingDef{ "maxDistance", 1000.0f, 0.0f, 10000.0f, {}, "Distances are clamped to this (and used when there is no target)" });
			def.execute = ExecuteDistanceTo;
			registry.Register(std::move(def));
		}
	}
}
