// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Spawner nodes. A spawner turns points into things to place: it outputs a copy of its input on "Spawns" in
// which every point carries a String column "asset", and every item the attributes "kind" (String) and
// "collides" (Bool). CollectPlacements (graph_output.h) turns those items into placements. "Spawns" is terminal:
// it is collected as emitted, and linking it into further nodes does not change what is placed (CollectPlacements
// warns about such links); filter and transform points before the spawner.
//
// SpawnMeshes  settings: meshes (weighted list "path|weight;path|weight"; a missing weight is 1; entries with
//               a weight that is not a positive finite number are ignored with a warning), kind (Entity |
//               Foliage | WorldModel), collides, assetAttribute (name of a String point attribute; a point
//               whose attribute is a non-empty String uses it as the asset instead of the weighted pick).
//               The pick is weighted by HashToUnit(Hash(nodeSeed, point seed)), so a point's asset depends
//               only on its seed and the node seed. Points without an asset (empty list, or weights that sum
//               to 0, and no override) are dropped with one warning. Items left without points are omitted.
//               Tags, item attributes and sourceId of the input items are kept. Reads nothing from the context.

#include "builtin_nodes.h"
#include "evaluator.h"
#include "node_registry.h"
#include "node_util.h"

#include "pcg/pcg_random.h"

#include <cmath>
#include <cstdlib>

namespace mmo::pcg::graph
{
	namespace
	{
		struct WeightedAsset
		{
			String path;
			double weight = 1.0;
		};

		String Trim(const String& s)
		{
			const size_t first = s.find_first_not_of(" \t");
			if (first == String::npos)
			{
				return String();
			}
			const size_t last = s.find_last_not_of(" \t");
			return s.substr(first, last - first + 1);
		}

		/// Parses "path|weight;path|weight". Invalid entries are skipped with a warning.
		std::vector<WeightedAsset> ParseMeshes(NodeOutput& out, const String& list)
		{
			std::vector<WeightedAsset> result;
			size_t start = 0;
			while (start <= list.size())
			{
				size_t end = list.find(';', start);
				if (end == String::npos)
				{
					end = list.size();
				}
				const String entry = Trim(list.substr(start, end - start));
				start = end + 1;
				if (entry.empty())
				{
					continue;
				}

				WeightedAsset asset;
				const size_t bar = entry.find('|');
				if (bar == String::npos)
				{
					asset.path = entry;
				}
				else
				{
					asset.path = Trim(entry.substr(0, bar));
					const String weightText = Trim(entry.substr(bar + 1));
					char* parseEnd = nullptr;
					asset.weight = std::strtod(weightText.c_str(), &parseEnd);
					if (weightText.empty() || parseEnd == nullptr || *parseEnd != '\0' || !std::isfinite(asset.weight) || asset.weight < 0.0)
					{
						WarnOnce(out, "ignored a mesh entry with an invalid weight");
						continue;
					}
				}
				if (asset.path.empty())
				{
					WarnOnce(out, "ignored a mesh entry without a path");
					continue;
				}
				if (asset.weight > 0.0)
				{
					result.push_back(std::move(asset));
				}
			}
			return result;
		}

		NodeOutput ExecuteSpawnMeshes(const EvalContext&, const uint64 nodeSeed, const AttributeSet& settings, const std::map<String, Collection>& inputs)
		{
			NodeOutput out;
			Collection& spawns = out.pins["Spawns"];

			const String kind = SettingString(settings, "kind", "Foliage");
			if (kind != "Entity" && kind != "Foliage" && kind != "WorldModel")
			{
				out.error = "unknown kind '" + kind + "'";
				return out;
			}
			const bool collides = SettingBool(settings, "collides", true);
			const String assetAttribute = SettingString(settings, "assetAttribute", "");
			const std::vector<WeightedAsset> assets = ParseMeshes(out, SettingString(settings, "meshes", ""));
			double total = 0.0;
			for (const WeightedAsset& a : assets)
			{
				total += a.weight;
			}

			const Collection* in = FindInput(inputs, "In");
			if (in == nullptr)
			{
				return out;
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
				const size_t count = item.points.points.size();
				std::vector<AttributeValue> assetColumn(count, AttributeValue(String()));
				std::vector<uint8> keep(count, 0);
				size_t kept = 0;
				bool dropped = false;

				for (size_t i = 0; i < count; ++i)
				{
					String asset;
					if (!assetAttribute.empty())
					{
						AttributeValue value;
						if (ReadPointValue(source, i, assetAttribute, value))
						{
							const String* text = std::get_if<String>(&value);
							if (text != nullptr)
							{
								asset = *text;
							}
						}
					}
					if (asset.empty() && total > 0.0)
					{
						const double u = static_cast<double>(HashToUnit(Hash(nodeSeed, static_cast<int64>(source.points.points[i].seed)))) * total;
						double cumulative = 0.0;
						asset = assets.back().path;
						for (const WeightedAsset& a : assets)
						{
							cumulative += a.weight;
							if (u < cumulative)
							{
								asset = a.path;
								break;
							}
						}
					}

					if (asset.empty())
					{
						dropped = true;
						continue;
					}
					assetColumn[i] = AttributeValue(asset);
					keep[i] = 1;
					++kept;
				}

				if (dropped)
				{
					WarnOnce(out, "dropped points without an asset (empty or zero weight mesh list)");
				}
				if (kept == 0)
				{
					continue;
				}

				item.points.columns["asset"] = { AttributeType::String, std::move(assetColumn) };
				item.points.Keep(keep);
				item.attributes["kind"] = kind;
				item.attributes["collides"] = collides;
				spawns.push_back(std::move(item));
			}
			return out;
		}
	}

	void RegisterSpawnNodes(NodeRegistry& registry)
	{
		NodeTypeDef def;
		def.type = "SpawnMeshes";
		def.category = "Spawners";
		def.title = "Spawn Meshes";
		def.inputs.push_back(PointsInputPin());
		def.outputs.push_back(PinDef{ "Spawns", { ItemKind::Points } });

		SettingDef meshes{ "meshes", String(), 0, 0, {}, "Weighted mesh list: path|weight;path|weight" };
		meshes.widget = "assetList:.hmsh;.hwmo";
		def.settings.push_back(std::move(meshes));
		def.settings.push_back(SettingDef{ "kind", String("Foliage"), 0, 0, { "Entity", "Foliage", "WorldModel" }, "What kind of scene object a point becomes" });
		def.settings.push_back(SettingDef{ "collides", true, 0, 0, {}, "Whether the placed objects collide" });
		def.settings.push_back(SettingDef{ "assetAttribute", String(), 0, 0, {}, "String point attribute that overrides the weighted pick (empty = off)" });
		def.contextDeps = context_dep::None;
		def.execute = ExecuteSpawnMeshes;
		registry.Register(std::move(def));
	}
}
