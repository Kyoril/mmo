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
// StampSectionAtPoints   In: Points items. One Terrain item per point, in point order: a two point linear spline
//                        of `length` metres along the point's facing (rotation applied to +Z, flattened), centred on
//                        the point or starting at it (`anchor`), at the point's y; pointIds = Hash(point seed, 0|1).
//                        Length 0 = the section's own length, which only the terrain stage knows: the op then carries
//                        StampLengthParam / StampAnchorParam (terrain_op.h) and PreparedTerrain lays the ends out.
//                        heightMode defaults to followSpline.
//
// Errors (node errors, found at execute time): an empty section path, or a parameter value that
// SetSectionPathParam rejects. Whether the asset exists is checked when the terrain is prepared.

#include "builtin_nodes.h"
#include "evaluator.h"
#include "node_registry.h"
#include "node_util.h"

#include "pcg/pcg_random.h"
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

		/// Resolves every section parameter of one item into `params`: the item attribute of that name wins over the
		/// setting, which wins over the default (`heightModeDefault` is the default of heightMode). On a rejected
		/// value sets out.error, clears the pins and returns false.
		bool ResolveSectionParams(NodeOutput& out, const DataItem& source, const AttributeSet& settings, const char* heightModeDefault, AttributeSet& params)
		{
			SectionPathParams scratch;
			for (const ParamDef& param : SectionParams())
			{
				AttributeValue value = param.defaultValue;
				if (String(param.name) == "heightMode")
				{
					value = String(heightModeDefault);
				}
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
					return false;
				}
				if (!SetSectionPathParam(scratch, param.name, text, error))
				{
					out.error = String("invalid ") + origin + " '" + param.name + "': " + error;
					out.pins.clear();
					return false;
				}
				params[param.name] = std::move(value);
			}
			return true;
		}

		/// Adds one setting per section parameter (floorInside only when `withFloorInside`), heightMode defaulting to
		/// `heightModeDefault`.
		void AddSectionParamSettings(NodeTypeDef& def, const bool withFloorInside, const char* heightModeDefault)
		{
			for (const ParamDef& param : SectionParams())
			{
				const String name = param.name;
				if (name == "floorInside" && !withFloorInside)
				{
					continue;
				}
				SettingDef setting{ name, param.defaultValue, 0, 0, {}, param.description };
				if (name == "heightMode")
				{
					setting.defaultValue = String(heightModeDefault);
					setting.enumValues = { "followSpline", "followGround", "smoothedGround" };
				}
				def.settings.push_back(std::move(setting));
			}
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

				if (!ResolveSectionParams(out, source, settings, "smoothedGround", item.op.params))
				{
					return out;
				}

				terrain.push_back(std::move(item));
			}
			return out;
		}

		NodeOutput ExecuteStamp(const AttributeSet& settings, const std::map<String, Collection>& inputs)
		{
			NodeOutput out;
			Collection& terrain = out.pins["Terrain"];

			const String section = SettingString(settings, "section", String());
			if (section.empty())
			{
				out.error = "no section: the 'section' setting is empty";
				out.pins.clear();
				return out;
			}
			const Collection* in = FindInput(inputs, "In");
			if (in == nullptr)
			{
				return out;
			}
			const String anchor = SettingString(settings, "anchor", "center");
			if (anchor != "center" && anchor != "start")
			{
				out.error = "invalid setting 'anchor': expected center or start";
				out.pins.clear();
				return out;
			}
			const bool centered = anchor == "center";
			const float length = SettingFloat(settings, "length", 0.0f);
			if (length < 0.0f)
			{
				out.error = "invalid setting 'length': must not be negative";
				out.pins.clear();
				return out;
			}
			// Length 0 means the asset's length, which only the terrain stage knows: the spline is then laid out for
			// a unit length and the terrain stage stretches it (see terrain_compose.cpp, kStampLength).
			const bool assetLength = length <= 0.0f;
			const float span = assetLength ? 1.0f : length;

			for (const DataItem& source : *in)
			{
				if (source.kind != ItemKind::Points)
				{
					WarnOnce(out, "skipped items that are not points");
					continue;
				}

				for (const Point& point : source.points.points)
				{
					Vector3 forward = point.rotation * Vector3(0.0f, 0.0f, 1.0f);
					const float flat = std::hypot(forward.x, forward.z);
					if (!(flat > 1e-4f))
					{
						WarnOnce(out, "a point points straight up or down: its stamp lies along +Z");
						forward = Vector3(0.0f, 0.0f, 1.0f);
					}
					else
					{
						forward = Vector3(forward.x / flat, 0.0f, forward.z / flat);
					}

					const float from = centered ? -span * 0.5f : 0.0f;
					SplinePoint a;
					a.x = point.position.x + forward.x * from;
					a.z = point.position.z + forward.z * from;
					a.y = point.position.y;
					SplinePoint b;
					b.x = point.position.x + forward.x * (from + span);
					b.z = point.position.z + forward.z * (from + span);
					b.y = point.position.y;

					DataItem item;
					item.kind = ItemKind::Terrain;
					item.tags = source.tags;
					item.sourceId = source.sourceId;
					item.spline.closed = false;
					item.spline.interpolation = SplineInterpolation::Linear;
					item.spline.points = { a, b };
					item.pointIds = { Hash(point.seed, 0), Hash(point.seed, 1) };
					item.op.type = TerrainOpType::SectionPath;
					item.op.asset = section;
					item.op.stackOrder = ResolveStackOrder(out, source, settings);
					if (!ResolveSectionParams(out, source, settings, "followSpline", item.op.params))
					{
						return out;
					}
					if (assetLength)
					{
						item.op.params[StampLengthParam] = 0.0f;
						item.op.params[StampAnchorParam] = anchor;
					}
					terrain.push_back(std::move(item));
				}
			}
			return out;
		}

		void RegisterStamp(NodeRegistry& registry)
		{
			NodeTypeDef def;
			def.type = "StampSectionAtPoints";
			def.category = "Spawners";
			def.title = "Stamp Section at Points";
			def.inputs.push_back(PinDef{ "In", { ItemKind::Points }, true, true });
			def.outputs.push_back(PinDef{ "Terrain", { ItemKind::Terrain } });

			SettingDef section{ "section", String(), 0, 0, {}, "Section asset (.hsec) stamped at every point" };
			section.widget = "asset:.hsec";
			def.settings.push_back(std::move(section));
			SettingDef anchor{ "anchor", String("center"), 0, 0, {}, "Whether the point is the middle or the start of the stamp" };
			anchor.enumValues = { "center", "start" };
			def.settings.push_back(std::move(anchor));
			def.settings.push_back(SettingDef{ "length", 0.0f, 0, 1000, {}, "Length (m) of the stamp along the point's facing; 0 = the section's own length" });

			AddSectionParamSettings(def, false, "followSpline");
			def.settings.push_back(StackOrderSetting());

			def.contextDeps = context_dep::None;
			def.execute = [](const EvalContext&, uint64, const AttributeSet& settings, const std::map<String, Collection>& inputs)
			{
				return ExecuteStamp(settings, inputs);
			};
			registry.Register(std::move(def));
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

			AddSectionParamSettings(def, loop, "smoothedGround");
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
		RegisterStamp(registry);
	}
}
