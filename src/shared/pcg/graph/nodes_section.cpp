// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Section nodes. They lay a hand-built section (.hsec: roads, cliff walls) along splines or closed loops by
// emitting one Terrain item per input item, carrying a SectionPath terrain op (terrain_op.h) that the terrain
// stage prepares (terrain_compose.h).
//
// LaySectionAlongSpline  In: Spline items (open or closed). Has no floorInside setting, but its ops still carry
//                        floorInside = true (the parameter default, or an item attribute of that name): it decides
//                        which side is the floor on a closed spline (open paths ignore it).
// LaySectionAlongLoop    In: Area items (closed loops); an Area whose spline is not closed is a node error.
//                        Has the extra setting floorInside; the output spline is closed.
//
// Output "Terrain": one item per input item, in input order, copying spline, pointIds, sourceId and tags. The op
// has type SectionPath, asset = the resolved .hsec path and params = every section parameter under its
// SetSectionPathParam name. The evaluator stamps op.order with the node id.
//
// Settings: section (.hsec path), sectionAttribute (name of an item attribute whose non-empty String value wins over
// `section`), and one setting per section parameter. An item attribute named like a section parameter overrides
// the setting for that item. Reads nothing from the context.
//
// Errors (node errors, found at execute time): an empty section path, or a parameter value that
// SetSectionPathParam rejects. Whether the asset exists is checked when the terrain is prepared.

#include "builtin_nodes.h"
#include "evaluator.h"
#include "node_registry.h"
#include "node_util.h"

#include "pcg/section_path.h"

#include <cmath>
#include <variant>

namespace mmo::pcg::graph
{
	namespace
	{
		enum class ParamKind : uint8
		{
			Float,
			Int,
			Bool,
			Enum
		};

		struct ParamDef
		{
			const char* name;
			ParamKind kind;
			AttributeValue defaultValue;
			const char* description;
		};

		const std::vector<ParamDef>& SectionParams()
		{
			static const std::vector<ParamDef> params = {
				{ "seed", ParamKind::Int, int64(1), "Seed of the variant choice and the prop variation" },
				{ "sequentialVariants", ParamKind::Bool, false, "Variants in layout order instead of hashed per copy" },
				{ "joinBlend", ParamKind::Float, 2.0f, "Distance (m) over which the terrain targets of two copies cross-fade" },
				{ "propDensity", ParamKind::Float, 1.0f, "Multiplies every prop rule's keep chance (0..1)" },
				{ "heightMode", ParamKind::Enum, String("smoothedGround"), "Where the centre line height comes from" },
				{ "floorInside", ParamKind::Bool, true, "Closed paths: the floor side faces the loop interior" },
				{ "groundSmoothing", ParamKind::Float, 20.0f, "smoothedGround: length (m) the base ground is averaged over" },
				{ "massEnabled", ParamKind::Bool, false, "Adds a broad terrain mass behind the section" },
				{ "massHeight", ParamKind::Float, 30.0f, "Mass height (m) above the centre line" },
				{ "massDepth", ParamKind::Float, 60.0f, "How far (m) the mass reaches across" },
				{ "massRise", ParamKind::Float, 0.25f, "Fraction of the depth over which the mass rises" },
				{ "massNoiseAmplitude", ParamKind::Float, 15.0f, "Low-frequency variation (m) of the mass height" },
				{ "massNoiseWavelength", ParamKind::Float, 120.0f, "Wavelength (m) of that variation" },
				{ "massDetailAmplitude", ParamKind::Float, 3.0f, "Breakup (m) on top of it" },
				{ "massDetailWavelength", ParamKind::Float, 25.0f, "Wavelength (m) of the breakup" },
				{ "massPaintLayer", ParamKind::Int, int64(-1), "Section paint layer (0..3) of the mass slopes, -1 = automatic" },
				{ "mirrorCopies", ParamKind::Bool, false, "Lay every copy mirrored along its length or not, by hash" },
				{ "facadeHeightJitter", ParamKind::Float, 0.0f, "Scales every copy's section heights by a hashed factor in 1 +- jitter" },
			};
			return params;
		}

		/// Brings a value to the canonical type of its parameter where that loses nothing; others stay as they are
		/// (and are then rejected or accepted by SetSectionPathParam).
		AttributeValue Normalize(const ParamKind kind, const AttributeValue& value)
		{
			switch (kind)
			{
			case ParamKind::Float:
			{
				float number = 0.0f;
				if (!std::holds_alternative<bool>(value) && ToNumber(value, number))
				{
					return number;
				}
				break;
			}
			case ParamKind::Int:
				if (const float* f = std::get_if<float>(&value))
				{
					if (std::isfinite(*f) && std::floor(*f) == *f && std::abs(*f) < 9.0e18f)
					{
						return static_cast<int64>(*f);
					}
				}
				break;
			case ParamKind::Bool:
				if (const int64* i = std::get_if<int64>(&value))
				{
					if (*i == 0 || *i == 1)
					{
						return *i == 1;
					}
				}
				if (const float* f = std::get_if<float>(&value))
				{
					if (*f == 0.0f || *f == 1.0f)
					{
						return *f == 1.0f;
					}
				}
				break;
			case ParamKind::Enum:
				break;
			}
			return value;
		}

		NodeOutput ExecuteLaySection(const bool loop, const AttributeSet& settings, const std::map<String, Collection>& inputs)
		{
			NodeOutput out;
			Collection& terrain = out.pins["Terrain"];

			const Collection* in = FindInput(inputs, "In");
			if (in == nullptr)
			{
				return out;
			}

			const String sectionSetting = SettingString(settings, "section", String());
			const String sectionAttribute = SettingString(settings, "sectionAttribute", "section");
			const ItemKind expected = loop ? ItemKind::Area : ItemKind::Spline;

			for (const DataItem& source : *in)
			{
				if (source.kind != expected)
				{
					WarnOnce(out, loop ? "skipped items that are not areas" : "skipped items that are not splines");
					continue;
				}
				if (loop && !source.spline.closed)
				{
					out.error = "an area's loop is not closed";
					out.pins.clear();
					return out;
				}

				DataItem item;
				item.kind = ItemKind::Terrain;
				item.tags = source.tags;
				item.sourceId = source.sourceId;
				item.spline = source.spline;
				item.spline.closed = loop ? true : source.spline.closed;
				item.pointIds = source.pointIds;
				item.op.type = TerrainOpType::SectionPath;
				item.op.stackOrder = ResolveStackOrder(out, source, settings);

				String section = sectionSetting;
				if (!sectionAttribute.empty())
				{
					const auto it = source.attributes.find(sectionAttribute);
					if (it != source.attributes.end())
					{
						const String* text = std::get_if<String>(&it->second);
						if (text != nullptr && !text->empty())
						{
							section = *text;
						}
						else if (text == nullptr)
						{
							WarnOnce(out, "ignored the item attribute '" + sectionAttribute + "': it is not a String");
						}
					}
				}
				if (section.empty())
				{
					out.error = "no section: the 'section' setting and the item attribute '" + sectionAttribute + "' are empty";
					out.pins.clear();
					return out;
				}
				item.op.asset = section;

				SectionPathParams scratch;
				for (const ParamDef& param : SectionParams())
				{
					AttributeValue value = param.defaultValue;
					const char* origin = "setting";
					const auto fromItem = source.attributes.find(param.name);
					if (fromItem != source.attributes.end())
					{
						value = fromItem->second;
						origin = "item attribute";
					}
					else
					{
						const auto fromSettings = settings.find(param.name);
						if (fromSettings != settings.end())
						{
							value = fromSettings->second;
						}
					}
					value = Normalize(param.kind, value);

					String text;
					String error;
					if (!SectionParamText(value, text))
					{
						out.error = String("invalid ") + origin + " '" + param.name + "': the value has an unsupported type";
						out.pins.clear();
						return out;
					}
					if (!SetSectionPathParam(scratch, param.name, text, error))
					{
						out.error = String("invalid ") + origin + " '" + param.name + "': " + error;
						out.pins.clear();
						return out;
					}
					item.op.params[param.name] = std::move(value);
				}

				terrain.push_back(std::move(item));
			}
			return out;
		}

		void RegisterLaySection(NodeRegistry& registry, const bool loop)
		{
			NodeTypeDef def;
			def.type = loop ? "LaySectionAlongLoop" : "LaySectionAlongSpline";
			def.category = "Sections";
			def.title = loop ? "Lay Section Along Loop" : "Lay Section Along Spline";
			def.inputs.push_back(PinDef{ "In", { loop ? ItemKind::Area : ItemKind::Spline }, true, true });
			def.outputs.push_back(PinDef{ "Terrain", { ItemKind::Terrain } });

			SettingDef section{ "section", String(), 0, 0, {}, "Section asset (.hsec); used when the item has no section attribute" };
			section.widget = "asset:.hsec";
			def.settings.push_back(std::move(section));
			def.settings.push_back(SettingDef{ "sectionAttribute", String("section"), 0, 0, {}, "Item attribute whose String value overrides the section (empty = off)" });

			for (const ParamDef& param : SectionParams())
			{
				const String name = param.name;
				if (name == "floorInside" && !loop)
				{
					continue;
				}
				SettingDef setting{ name, param.defaultValue, 0, 0, {}, param.description };
				if (name == "heightMode")
				{
					setting.enumValues = { "followSpline", "followGround", "smoothedGround" };
				}
				def.settings.push_back(std::move(setting));
			}
			def.settings.push_back(StackOrderSetting());

			def.contextDeps = context_dep::None;
			def.execute = [loop](const EvalContext&, uint64, const AttributeSet& settings, const std::map<String, Collection>& inputs)
			{
				return ExecuteLaySection(loop, settings, inputs);
			};
			registry.Register(std::move(def));
		}
	}

	void RegisterSectionNodes(NodeRegistry& registry)
	{
		RegisterLaySection(registry, false);
		RegisterLaySection(registry, true);
	}
}
