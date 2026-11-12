// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Terrain shape nodes. Each emits one Terrain item per input item, in input order, copying spline, pointIds,
// sourceId and tags, and carrying a terrain op (terrain_op.h) whose params are the ones PreparedTerrain::Prepare
// reads (terrain_compose.h). The evaluator stamps op.order with the node id.
//
// TerrainNoise       In: Area.            Params: amplitude, wavelength, octaves, falloff, seed.
// RaiseAlongSpline   In: Spline.          Params: width (per side), falloffPower.
// CarveAlongSpline   In: Spline.          Params: width (per side), depth, bank.
// PaintSplat         In: Area or Spline.  Params: texture (String), weight, falloff, and width on open splines (per side);
//                                         tileMaterial (String) only when not empty.
// WaterArea          In: Area.            Params: level, levelOffset, margin, depth, shoreWidth; op.asset = water material (.hmat).
// River              In: Spline.          Params: width (per side), bank, depth, surfaceOffset, waterMargin; op.asset = water material (.hmat).
//
// Every setting can be overridden per item by an item attribute of the same name; a numeric setting only by a
// numeric (Float or Int) attribute, a String setting only by a String one (other types are ignored with a warning).
// TerrainNoise writes its `seed` as the node seed (Hash of the instance seed and node id) unless an item
// attribute `seed` overrides it, so the noise differs per graph instance. Reads nothing from the context.

#include "builtin_nodes.h"
#include "evaluator.h"
#include "node_registry.h"
#include "node_util.h"

#include <cmath>
#include <variant>

namespace mmo::pcg::graph
{
	namespace
	{
		/// A number: the item attribute if it is numeric, else the setting.
		float ResolveNumber(NodeOutput& out, const DataItem& source, const AttributeSet& settings, const char* name, const float fallback)
		{
			const auto it = source.attributes.find(name);
			if (it != source.attributes.end())
			{
				float value = 0.0f;
				if (!std::holds_alternative<bool>(it->second) && ToNumber(it->second, value))
				{
					return value;
				}
				WarnOnce(out, String("ignored the item attribute '") + name + "': it is not a number");
			}
			return SettingFloat(settings, name, fallback);
		}

		/// A String: the item attribute if it is a String, else the setting.
		String ResolveString(NodeOutput& out, const DataItem& source, const AttributeSet& settings, const char* name)
		{
			const auto it = source.attributes.find(name);
			if (it != source.attributes.end())
			{
				if (const String* text = std::get_if<String>(&it->second))
				{
					return *text;
				}
				WarnOnce(out, String("ignored the item attribute '") + name + "': it is not a String");
			}
			return SettingString(settings, name, String());
		}

		struct NumberParam
		{
			const char* name;
			float defaultValue;
			const char* description;
			/// UI range; used when max > min.
			float min = 0.0f;
			float max = 0.0f;
		};

		/// Everything that differs between the nodes.
		struct TerrainNodeDef
		{
			const char* type;
			const char* title;
			TerrainOpType opType;
			std::vector<ItemKind> accepts;
			std::vector<NumberParam> numbers;
			const char* itemWord;
		};

		const TerrainNodeDef& NodeDefOf(const TerrainOpType type)
		{
			static const TerrainNodeDef noise{ "TerrainNoise", "Terrain Noise", TerrainOpType::Noise, { ItemKind::Area },
				{ { "amplitude", 1.5f, "Height range (m) of the noise, +- amplitude" },
				  { "wavelength", 24.0f, "Wavelength (m) of the lowest octave" },
				  { "octaves", 3.0f, "Number of octaves (1..8)" },
				  { "falloff", 8.0f, "Distance (m) from the area edge over which the noise fades in" } },
				"areas" };
			static const TerrainNodeDef raise{ "RaiseAlongSpline", "Raise Along Spline", TerrainOpType::Raise, { ItemKind::Spline },
				{ { "width", 40.0f, "Distance (m) per side over which the raise falls off" },
				  { "falloffPower", 2.0f, "Exponent of the falloff profile" } },
				"splines" };
			static const TerrainNodeDef carve{ "CarveAlongSpline", "Carve Along Spline", TerrainOpType::Carve, { ItemKind::Spline },
				{ { "width", 2.0f, "Half width (m) of the full-depth bed, per side" },
				  { "depth", 1.5f, "Depth (m) of the bed" },
				  { "bank", 3.0f, "Distance (m) per side over which the bank rises to the ground" } },
				"splines" };
			static const TerrainNodeDef paint{ "PaintSplat", "Paint Splat", TerrainOpType::Paint, { ItemKind::Area, ItemKind::Spline },
				{ { "weight", 1.0f, "Paint strength (0..1)" },
				  { "width", 4.0f, "Open splines: distance (m) per side that is painted" },
				  { "falloff", 2.0f, "Distance (m) over which the paint fades out at its edge" } },
				"areas or splines" };
			static const TerrainNodeDef water{ "WaterArea", "Water Area", TerrainOpType::WaterArea, { ItemKind::Area },
				{ { "level", 0.0f, "Water surface height (m); the item attribute 'level' overrides it" },
				  { "levelOffset", 0.0f, "Added to the level (m)" },
				  { "margin", 4.0f, "Distance (m) past the shore that still holds water" },
				  { "depth", 0.0f, "Basin depth (m) below the water level; 0 leaves the ground alone", 0.0f, 200.0f },
				  { "shoreWidth", 6.0f, "Distance (m) inwards over which the shore slopes down to the bed", 0.5f, 200.0f } },
				"areas" };
			static const TerrainNodeDef river{ "River", "River", TerrainOpType::River, { ItemKind::Spline },
				{ { "width", 5.0f, "Half width (m) of the bed floor, per side" },
				  { "bank", 4.0f, "Distance (m) beyond the bed over which the bank rises back to the ground" },
				  { "depth", 1.5f, "Depth (m) of the bed below the water surface" },
				  { "surfaceOffset", -0.4f, "Water surface relative to the spline height (m); the surface never climbs downstream" },
				  { "waterMargin", 3.0f, "Distance (m) past the bank's top that still holds water (keep it >= the terrain quad's half diagonal, ~3 m)" } },
				"splines" };
			switch (type)
			{
			case TerrainOpType::Noise:
				return noise;
			case TerrainOpType::Raise:
				return raise;
			case TerrainOpType::Carve:
				return carve;
			case TerrainOpType::Paint:
				return paint;
			case TerrainOpType::River:
				return river;
			default:
				return water;
			}
		}

		NodeOutput ExecuteTerrainNode(const TerrainOpType opType, const uint64 nodeSeed, const AttributeSet& settings,
			const std::map<String, Collection>& inputs)
		{
			const TerrainNodeDef& def = NodeDefOf(opType);
			NodeOutput out;
			Collection& terrain = out.pins["Terrain"];

			const Collection* in = FindInput(inputs, "In");
			if (in == nullptr)
			{
				return out;
			}

			for (const DataItem& source : *in)
			{
				bool accepted = false;
				for (const ItemKind kind : def.accepts)
				{
					accepted = accepted || source.kind == kind;
				}
				if (!accepted)
				{
					WarnOnce(out, String("skipped items that are not ") + def.itemWord);
					continue;
				}

				DataItem item;
				item.kind = ItemKind::Terrain;
				item.tags = source.tags;
				item.sourceId = source.sourceId;
				item.spline = source.spline;
				item.pointIds = source.pointIds;
				item.op.type = opType;
				item.op.stackOrder = ResolveStackOrder(out, source, settings);

				for (const NumberParam& param : def.numbers)
				{
					if (opType == TerrainOpType::Paint && source.spline.closed && String(param.name) == "width")
					{
						continue;
					}
					item.op.params[param.name] = ResolveNumber(out, source, settings, param.name, param.defaultValue);
				}

				switch (opType)
				{
				case TerrainOpType::Noise:
				{
					int64 seed = static_cast<int64>(nodeSeed);
					const auto it = source.attributes.find("seed");
					if (it != source.attributes.end())
					{
						// An Int is taken as is (every int64 is a valid seed); a Float is rounded if it fits an int64.
						float number = 0.0f;
						if (const int64* integer = std::get_if<int64>(&it->second))
						{
							seed = *integer;
						}
						else if (!std::holds_alternative<bool>(it->second) && ToNumber(it->second, number) && std::abs(number) < 9.0e18f)
						{
							seed = static_cast<int64>(std::llround(number));
						}
						else
						{
							WarnOnce(out, "ignored the item attribute 'seed': it is not a number");
						}
					}
					item.op.params["seed"] = seed;
					break;
				}
				case TerrainOpType::Paint:
				{
					const String texture = ResolveString(out, source, settings, "texture");
					if (texture.empty())
					{
						// Prepare skips such an op; say so where the graph is edited.
						WarnOnce(out, "no texture: the 'texture' setting and the item attribute are empty, nothing is painted");
					}
					item.op.params["texture"] = texture;
					// Only a named material enters the op, so a node without one hashes as before the setting existed.
					const String tileMaterial = ResolveString(out, source, settings, "tileMaterial");
					if (!tileMaterial.empty())
					{
						item.op.params["tileMaterial"] = tileMaterial;
					}
					break;
				}
				case TerrainOpType::WaterArea:
				case TerrainOpType::River:
				{
					item.op.asset = ResolveString(out, source, settings, "material");
					break;
				}
				default:
					break;
				}

				terrain.push_back(std::move(item));
			}
			return out;
		}

		void RegisterTerrainNode(NodeRegistry& registry, const TerrainOpType opType)
		{
			const TerrainNodeDef& source = NodeDefOf(opType);

			NodeTypeDef def;
			def.type = source.type;
			def.category = "Terrain";
			def.title = source.title;
			def.inputs.push_back(PinDef{ "In", source.accepts, true, true });
			def.outputs.push_back(PinDef{ "Terrain", { ItemKind::Terrain } });

			if (opType == TerrainOpType::Paint)
			{
				SettingDef texture{ "texture", String(), 0, 0, {}, "Texture (.htex) to paint; used when the item has no texture attribute" };
				texture.widget = "asset:.htex";
				def.settings.push_back(std::move(texture));
				SettingDef tileMaterial{ "tileMaterial", String(), 0, 0, {},
					"Tile material (.hmat/.hmi) every painted tile is switched to, so the texture is one of its layers; empty = keep the tile's material" };
				tileMaterial.widget = "asset:.hmat;.hmi";
				def.settings.push_back(std::move(tileMaterial));
			}
			for (const NumberParam& param : source.numbers)
			{
				def.settings.push_back(SettingDef{ param.name, param.defaultValue, param.min, param.max, {}, param.description });
			}
			def.settings.push_back(StackOrderSetting());
			if (opType == TerrainOpType::WaterArea)
			{
				SettingDef material{ "material", String(), 0, 0, {}, "Water material (.hmat); empty = the terrain's default" };
				material.widget = "asset:.hmat";
				def.settings.push_back(std::move(material));
			}
			if (opType == TerrainOpType::River)
			{
				SettingDef material{ "material", String("Worlds/Water_River.hmat"), 0, 0, {}, "Water material (.hmat); it should read the flow" };
				material.widget = "asset:.hmat";
				def.settings.push_back(std::move(material));
			}

			def.contextDeps = context_dep::None;
			def.execute = [opType](const EvalContext&, const uint64 nodeSeed, const AttributeSet& settings, const std::map<String, Collection>& inputs)
			{
				return ExecuteTerrainNode(opType, nodeSeed, settings, inputs);
			};
			registry.Register(std::move(def));
		}
	}

	void RegisterTerrainNodes(NodeRegistry& registry)
	{
		RegisterTerrainNode(registry, TerrainOpType::Noise);
		RegisterTerrainNode(registry, TerrainOpType::Raise);
		RegisterTerrainNode(registry, TerrainOpType::Carve);
		RegisterTerrainNode(registry, TerrainOpType::Paint);
		RegisterTerrainNode(registry, TerrainOpType::WaterArea);
		RegisterTerrainNode(registry, TerrainOpType::River);
	}
}
