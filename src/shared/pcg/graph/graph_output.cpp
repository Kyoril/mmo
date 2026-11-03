// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "graph_output.h"

#include "json_util.h"
#include "node_util.h"

#include "pcg/pcg_random.h"

#include <algorithm>
#include <set>

namespace mmo::pcg::graph
{
	namespace
	{
		using jsonutil::Json;

		constexpr size_t DumpPointLimit = 16;

		const char* ItemKindName(const ItemKind kind)
		{
			switch (kind)
			{
			case ItemKind::Points: return "Points";
			case ItemKind::Spline: return "Spline";
			case ItemKind::Area: return "Area";
			case ItemKind::Params: return "Params";
			case ItemKind::Terrain: return "Terrain";
			}
			return "Params";
		}

		Json VectorJson(const Vector3& v)
		{
			return Json::array({ jsonutil::SafeDouble(v.x), jsonutil::SafeDouble(v.y), jsonutil::SafeDouble(v.z) });
		}

		Json ItemJson(const DataItem& item)
		{
			Json j = Json::object();
			j["kind"] = ItemKindName(item.kind);
			j["tags"] = Json::array();
			for (const String& tag : item.tags)
			{
				j["tags"].push_back(tag);
			}
			j["sourceId"] = item.sourceId;
			if (!item.pointIds.empty())
			{
				j["pointIds"] = item.pointIds;
			}
			if (item.kind == ItemKind::Terrain)
			{
				Json op = Json::object();
				op["type"] = TerrainOpTypeName(item.op.type);
				op["asset"] = item.op.asset;
				op["order"] = item.op.order;
				if (item.op.stackOrder != 0)
				{
					op["stackOrder"] = item.op.stackOrder;
				}
				Json params = Json::object();
				for (const auto& [name, value] : item.op.params)
				{
					params[name] = jsonutil::ValueToJson(value);
				}
				op["params"] = std::move(params);
				j["op"] = std::move(op);
			}
			Json attributes = Json::object();
			for (const auto& [name, value] : item.attributes)
			{
				attributes[name] = jsonutil::ValueToJson(value);
			}
			j["attributes"] = std::move(attributes);

			j["pointCount"] = item.points.points.size();
			Json points = Json::array();
			for (size_t i = 0; i < item.points.points.size() && i < DumpPointLimit; ++i)
			{
				const Point& p = item.points.points[i];
				Json pj = Json::object();
				pj["position"] = VectorJson(p.position);
				pj["rotation"] = Json::array({ jsonutil::SafeDouble(p.rotation.w), jsonutil::SafeDouble(p.rotation.x), jsonutil::SafeDouble(p.rotation.y), jsonutil::SafeDouble(p.rotation.z) });
				pj["scale"] = VectorJson(p.scale);
				pj["density"] = jsonutil::SafeDouble(p.density);
				pj["seed"] = p.seed;
				Json columns = Json::object();
				for (const auto& [name, column] : item.points.columns)
				{
					if (i < column.second.size())
					{
						columns[name] = jsonutil::ValueToJson(column.second[i]);
					}
				}
				pj["attributes"] = std::move(columns);
				points.push_back(std::move(pj));
			}
			j["points"] = std::move(points);
			return j;
		}
	}

	CollectedPlacements CollectPlacements(const Graph& graph, const EvalResult& result, const PathSpline& region)
	{
		CollectedPlacements collected;

		// "Spawns" pins are terminal: a link onward looks like post-processing but changes nothing collected here.
		std::set<uint32> linkedOnward;
		for (const Link& link : graph.links)
		{
			if (link.fromPin == "Spawns")
			{
				linkedOnward.insert(link.fromNode);
			}
		}
		for (const uint32 nodeId : linkedOnward)
		{
			collected.warnings.push_back("node " + std::to_string(nodeId) + ": its \"Spawns\" pin is linked onward, but \"Spawns\" "
				"pins are terminal: their points are collected as the node emits them, and what downstream nodes do with "
				"them is not collected");
		}

		// Region containment: the same indexed polygon test the samplers use.
		std::unique_ptr<RowIndexedPolygon> polygon;
		bool regionUsable = false;
		if (region.points.size() >= 3)
		{
			const Bounds2D bounds = SplineBounds(region);
			if (SaneBounds(bounds))
			{
				const SampledPath path(region, EvaluationStep(region, SamplingStep));
				if (path.IsClosed())
				{
					const float rowHeight = std::max(1.0f, (bounds.maxZ - bounds.minZ) / 4096.0f);
					polygon = std::make_unique<RowIndexedPolygon>(path.GetSamples(), rowHeight);
					regionUsable = true;
				}
			}
		}
		if (!regionUsable)
		{
			collected.warnings.push_back("the region is not a closed spline with at least 3 points: nothing was kept");
		}

		std::set<uint64> usedKeys;
		bool keyCollision = false;
		bool badKind = false;
		bool badAsset = false;

		for (const auto& [nodeId, nodeResult] : result.nodes)
		{
			const auto pin = nodeResult.output.pins.find("Spawns");
			if (pin == nodeResult.output.pins.end())
			{
				continue;
			}

			for (const DataItem& item : pin->second)
			{
				if (item.kind != ItemKind::Points)
				{
					continue;
				}
				const auto assetColumn = item.points.columns.find("asset");
				if (assetColumn == item.points.columns.end() || assetColumn->second.first != AttributeType::String)
				{
					if (!item.points.points.empty())
					{
						badAsset = true;
					}
					continue;
				}

				PlacementKind kind = PlacementKind::Entity;
				bool kindOk = false;
				const auto kindIt = item.attributes.find("kind");
				if (kindIt != item.attributes.end())
				{
					if (const String* name = std::get_if<String>(&kindIt->second))
					{
						kindOk = true;
						if (*name == "Entity")
						{
							kind = PlacementKind::Entity;
						}
						else if (*name == "Foliage")
						{
							kind = PlacementKind::Foliage;
						}
						else if (*name == "WorldModel")
						{
							kind = PlacementKind::WorldModel;
						}
						else
						{
							kindOk = false;
						}
					}
				}
				if (!kindOk)
				{
					badKind = true;
					continue;
				}

				bool collides = true;
				const auto collidesIt = item.attributes.find("collides");
				if (collidesIt != item.attributes.end())
				{
					if (const bool* flag = std::get_if<bool>(&collidesIt->second))
					{
						collides = *flag;
					}
				}

				for (size_t i = 0; i < item.points.points.size(); ++i)
				{
					const Point& point = item.points.points[i];
					const String* asset = i < assetColumn->second.second.size() ? std::get_if<String>(&assetColumn->second.second[i]) : nullptr;
					if (asset == nullptr || asset->empty())
					{
						badAsset = true;
						continue;
					}

					if (!regionUsable || !polygon->Contains(point.position.x, point.position.z))
					{
						++collected.clipped;
						continue;
					}

					uint64 key = Hash(nodeId, static_cast<int64>(item.sourceId), static_cast<int64>(point.seed));
					for (int64 n = 1; !usedKeys.insert(key).second; ++n)
					{
						keyCollision = true;
						key = Hash(key, n);
					}

					Placement placement;
					placement.key = key;
					placement.kind = kind;
					placement.asset = *asset;
					placement.position = point.position;
					placement.rotation = point.rotation;
					placement.scale = point.scale;
					placement.collides = collides;
					collected.placements.push_back(std::move(placement));
				}
			}
		}

		if (badAsset)
		{
			collected.warnings.push_back("skipped points without an asset");
		}
		if (badKind)
		{
			collected.warnings.push_back("skipped spawn items with a missing or unknown kind");
		}
		if (keyCollision)
		{
			collected.warnings.push_back("placement key collisions were resolved by re-hashing");
		}

		std::sort(collected.placements.begin(), collected.placements.end(), [](const Placement& a, const Placement& b)
		{
			return a.key < b.key;
		});
		return collected;
	}

	String WriteEvalDump(const Graph& graph, const EvalResult& result, const bool includeTiming)
	{
		Json root = Json::object();
		root["ok"] = result.ok;

		Json issues = Json::array();
		for (const GraphIssue& issue : result.issues)
		{
			Json j = Json::object();
			j["node"] = issue.node;
			j["message"] = issue.message;
			issues.push_back(std::move(j));
		}
		root["issues"] = std::move(issues);

		Json nodes = Json::object();
		for (const auto& [id, nodeResult] : result.nodes)
		{
			Json j = Json::object();
			String type;
			for (const NodeInstance& node : graph.nodes)
			{
				if (node.id == id)
				{
					type = node.type;
					break;
				}
			}
			j["type"] = type;
			if (includeTiming)
			{
				j["ms"] = jsonutil::SafeDouble(static_cast<float>(nodeResult.milliseconds));
				j["fromCache"] = nodeResult.fromCache;
			}
			j["warnings"] = nodeResult.output.warnings;
			j["error"] = nodeResult.output.error;

			Json pins = Json::object();
			for (const auto& [pinName, collection] : nodeResult.output.pins)
			{
				Json items = Json::array();
				for (const DataItem& item : collection)
				{
					items.push_back(ItemJson(item));
				}
				pins[pinName] = std::move(items);
			}
			j["pins"] = std::move(pins);
			nodes[std::to_string(id)] = std::move(j);
		}
		root["nodes"] = std::move(nodes);
		return jsonutil::Dump(root);
	}
}
