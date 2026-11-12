// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Point operation nodes. Each takes Points items on "In" and outputs one modified copy per input item on "Out"
// (tags, item attributes, sourceId and point order unchanged); items without points are left out, items of other
// kinds are skipped with a warning. Point seeds are never changed. Names of point properties (density,
// position.x|y|z, scale, scale.x|y|z) always mean the property (see IsReservedPointName).
//
// TransformPoints  settings: offsetMin, offsetMax (Vector3, world space), yawMin/yawMax (about +Y), pitchMin/
//               pitchMax (about +X), rollMin/rollMax (about +Z), all in degrees, scaleMin, scaleMax (Vector3;
//               uniformScale uses the x range for all axes and multiplies the point's scale), orientation (keep:
//               compose with the incoming rotation; upright: start from identity; alignToTerrain: start from the
//               rotation mapping +Y onto ctx.terrain's normal at the point, identity with a warning where there is
//               no terrain). Rotation = start * yaw * pitch * roll. Every random value is
//               HashRange(Hash(Hash(nodeSeed, point seed, 0, salt), channel), lo, hi): a point's result depends
//               only on its seed and the node seed. Reads only the terrain from the context (contextDeps Terrain).
// DensityNoise  settings: frequency, octaves (1-8), persistence, amount, mode (multiply | add | set). n =
//               FractalNoise(Hash(nodeSeed, 0, 0, salt), x, z, ...) in [-1, 1] at the point's world x/z, u = (n + 1)
//               / 2. multiply: d *= 1 - amount + amount * u; add: d += amount * n; set: d = (1 - amount) * d +
//               amount * u. The result is clamped to [0, 1]. Values depend on the world position only (not on the
//               point or its seed), so overlapping sample sets agree.
// AttributeMath settings: target (attribute or point property written), input (operand A; empty = the target),
//               op (add sub mul div min max clamp remap), operand (attribute for operand B; empty = the constant
//               value), value, min/max (clamp), inMin/inMax/outMin/outMax (remap; unclamped; a degenerate input
//               range gives outMin). Operands are read per point (point property, else column, else item
//               attribute) and must be numeric (Float, Int, Bool). With target scale and input empty or scale the
//               op runs per axis; a scalar written to scale sets all three axes. Rules (one warning each per
//               node): a missing or non-numeric operand leaves the point unchanged; division by zero and
//               non-finite results write 0; a target column of a non-Float type is left unchanged. A missing
//               target column is always created (Float, 0 for points that were skipped, even if all were).
// CopyAttributesFromControl  inputs "In" (Points), "Controls" (Areas, Splines). settings: attributes (comma list;
//               empty = every item attribute of any control), distance. Per point the source control is the first
//               Area (input order) containing it, else, if distance > 0, the nearest Spline or Area edge within
//               distance (ties: the earlier control). Area edges are distance sources on purpose: a point just
//               outside a lake (a river end) can take the lake's attributes. Areas whose spline is not closed are
//               skipped with a warning (PrepareControl). The source's item attributes are copied: reserved names set
//               the point property (numeric values only, else a warning), other names a column of the
//               attribute's type. Columns for every copied name are created up front (type of the first control
//               holding the name; points without a source keep the type default or their previous value). A
//               column that already exists with another type is left unchanged with a warning, as is a source
//               value whose type differs from the column's.

#include "builtin_nodes.h"
#include "evaluator.h"
#include "node_registry.h"
#include "node_util.h"

#include "math/constants.h"
#include "math/radian.h"
#include "pcg/pcg_random.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace mmo::pcg::graph
{
	namespace
	{
		constexpr uint64 TransformSalt = 0x7f0a3e11ull;
		constexpr uint64 NoiseSalt = 0xd0e5e5a1ull;

		Vector3 SettingVector(const AttributeSet& settings, const String& name, const Vector3& fallback)
		{
			const auto it = settings.find(name);
			if (it == settings.end())
			{
				return fallback;
			}
			const Vector3* v = std::get_if<Vector3>(&it->second);
			if (v == nullptr || !std::isfinite(v->x) || !std::isfinite(v->y) || !std::isfinite(v->z))
			{
				return fallback;
			}
			return *v;
		}

		/// Copies every Points item of "In" to "Out" and lets the visitor modify the copy.
		template <typename Visitor>
		NodeOutput ForEachPointsItem(const std::map<String, Collection>& inputs, NodeOutput out, Visitor&& visit)
		{
			Collection& result = out.pins["Out"];
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
				visit(out, item);
				result.push_back(std::move(item));
			}
			return out;
		}

		Radian Degrees(const float degrees)
		{
			return Radian(degrees * Pi / 180.0f);
		}

		NodeOutput ExecuteTransformPoints(const EvalContext& ctx, const uint64 nodeSeed, const AttributeSet& settings, const std::map<String, Collection>& inputs)
		{
			const Vector3 offsetMin = SettingVector(settings, "offsetMin", Vector3(0, 0, 0));
			const Vector3 offsetMax = SettingVector(settings, "offsetMax", Vector3(0, 0, 0));
			const float yawMin = SettingFloat(settings, "yawMin", 0.0f);
			const float yawMax = SettingFloat(settings, "yawMax", 0.0f);
			const float pitchMin = SettingFloat(settings, "pitchMin", 0.0f);
			const float pitchMax = SettingFloat(settings, "pitchMax", 0.0f);
			const float rollMin = SettingFloat(settings, "rollMin", 0.0f);
			const float rollMax = SettingFloat(settings, "rollMax", 0.0f);
			const Vector3 scaleMin = SettingVector(settings, "scaleMin", Vector3(1, 1, 1));
			const Vector3 scaleMax = SettingVector(settings, "scaleMax", Vector3(1, 1, 1));
			const bool uniformScale = SettingBool(settings, "uniformScale", true);
			const String orientation = SettingString(settings, "orientation", "keep");
			NodeOutput out;
			if (orientation != "keep" && orientation != "upright" && orientation != "alignToTerrain")
			{
				out.error = "unknown orientation '" + orientation + "'";
				out.pins["Out"];
				return out;
			}

			return ForEachPointsItem(inputs, std::move(out), [&](NodeOutput& o, DataItem& item)
			{
				for (Point& p : item.points.points)
				{
					const uint64 base = Hash(nodeSeed, static_cast<int64>(p.seed), 0, TransformSalt);
					auto channel = [base](const int64 k, const float lo, const float hi)
					{
						return HashRange(Hash(base, k), lo, hi);
					};

					p.position.x += channel(0, offsetMin.x, offsetMax.x);
					p.position.y += channel(1, offsetMin.y, offsetMax.y);
					p.position.z += channel(2, offsetMin.z, offsetMax.z);

					Quaternion start = p.rotation;
					if (orientation == "upright")
					{
						start = Quaternion::Identity;
					}
					else if (orientation == "alignToTerrain")
					{
						start = Quaternion::Identity;
						if (ctx.terrain == nullptr)
						{
							WarnOnce(o, "alignToTerrain: no terrain available, points kept upright");
						}
						else
						{
							start = RotationOntoNormal(ctx.terrain->Normal(p.position.x, p.position.z));
						}
					}
					const Quaternion yaw(Degrees(channel(3, yawMin, yawMax)), Vector3::UnitY);
					const Quaternion pitch(Degrees(channel(4, pitchMin, pitchMax)), Vector3::UnitX);
					const Quaternion roll(Degrees(channel(5, rollMin, rollMax)), Vector3::UnitZ);
					p.rotation = start * yaw * pitch * roll;
					p.rotation.Normalize();

					if (uniformScale)
					{
						const float s = channel(6, scaleMin.x, scaleMax.x);
						p.scale = Vector3(p.scale.x * s, p.scale.y * s, p.scale.z * s);
					}
					else
					{
						p.scale = Vector3(p.scale.x * channel(6, scaleMin.x, scaleMax.x), p.scale.y * channel(7, scaleMin.y, scaleMax.y), p.scale.z * channel(8, scaleMin.z, scaleMax.z));
					}
				}
			});
		}

		NodeOutput ExecuteDensityNoise(const EvalContext&, const uint64 nodeSeed, const AttributeSet& settings, const std::map<String, Collection>& inputs)
		{
			const float frequency = SettingFloat(settings, "frequency", 0.05f);
			const uint32 octaves = static_cast<uint32>(std::clamp(SettingFloat(settings, "octaves", 3.0f), 1.0f, 8.0f));
			const float persistence = SettingFloat(settings, "persistence", 0.5f);
			const float amount = SettingFloat(settings, "amount", 1.0f);
			const String mode = SettingString(settings, "mode", "multiply");
			NodeOutput out;
			if (mode != "multiply" && mode != "add" && mode != "set")
			{
				out.error = "unknown mode '" + mode + "'";
				out.pins["Out"];
				return out;
			}
			const uint64 noiseSeed = Hash(nodeSeed, 0, 0, NoiseSalt);

			return ForEachPointsItem(inputs, std::move(out), [&](NodeOutput&, DataItem& item)
			{
				for (Point& p : item.points.points)
				{
					float n = FractalNoise(noiseSeed, p.position.x, p.position.z, frequency, octaves, persistence);
					if (!std::isfinite(n))
					{
						n = 0.0f;
					}
					const float u = 0.5f * (n + 1.0f);
					float d = std::isfinite(p.density) ? p.density : 0.0f;
					if (mode == "multiply")
					{
						d *= 1.0f - amount + amount * u;
					}
					else if (mode == "add")
					{
						d += amount * n;
					}
					else
					{
						d = (1.0f - amount) * d + amount * u;
					}
					p.density = std::isfinite(d) ? std::clamp(d, 0.0f, 1.0f) : 0.0f;
				}
			});
		}

		enum class MathOp
		{
			Add,
			Sub,
			Mul,
			Div,
			Min,
			Max,
			Clamp,
			Remap,
			Invalid
		};

		MathOp ParseMathOp(const String& op)
		{
			static const std::pair<const char*, MathOp> ops[] = {
				{ "add", MathOp::Add }, { "sub", MathOp::Sub }, { "mul", MathOp::Mul }, { "div", MathOp::Div },
				{ "min", MathOp::Min }, { "max", MathOp::Max }, { "clamp", MathOp::Clamp }, { "remap", MathOp::Remap }
			};
			for (const auto& [name, value] : ops)
			{
				if (op == name)
				{
					return value;
				}
			}
			return MathOp::Invalid;
		}

		/// Settings of Attribute Math.
		struct MathRule
		{
			MathOp op = MathOp::Add;
			float min = 0.0f;
			float max = 1.0f;
			float inMin = 0.0f;
			float inMax = 1.0f;
			float outMin = 0.0f;
			float outMax = 1.0f;

			bool Binary() const
			{
				return op != MathOp::Clamp && op != MathOp::Remap;
			}

			float Apply(NodeOutput& out, const float a, const float b) const
			{
				float r = 0.0f;
				switch (op)
				{
				case MathOp::Add:
					r = a + b;
					break;
				case MathOp::Sub:
					r = a - b;
					break;
				case MathOp::Mul:
					r = a * b;
					break;
				case MathOp::Div:
					if (b == 0.0f)
					{
						WarnOnce(out, "division by zero, 0 written");
						return 0.0f;
					}
					r = a / b;
					break;
				case MathOp::Min:
					r = std::min(a, b);
					break;
				case MathOp::Max:
					r = std::max(a, b);
					break;
				case MathOp::Clamp:
					r = std::max(min, std::min(a, max));
					break;
				case MathOp::Remap:
					r = inMax != inMin ? outMin + (a - inMin) / (inMax - inMin) * (outMax - outMin) : outMin;
					break;
				default:
					break;
				}
				if (!std::isfinite(r))
				{
					WarnOnce(out, "non-finite result, 0 written");
					return 0.0f;
				}
				return r;
			}
		};

		bool ReadNumber(NodeOutput& out, const DataItem& item, const size_t index, const String& name, float& value)
		{
			AttributeValue v;
			if (!ReadPointValue(item, index, name, v))
			{
				WarnOnce(out, "operand '" + name + "' missing; points left unchanged");
				return false;
			}
			if (!ToNumber(v, value))
			{
				WarnOnce(out, "operand '" + name + "' is not a finite number; points left unchanged");
				return false;
			}
			return true;
		}

		NodeOutput ExecuteAttributeMath(const EvalContext&, uint64, const AttributeSet& settings, const std::map<String, Collection>& inputs)
		{
			NodeOutput out;
			const String target = SettingString(settings, "target", "density");
			const String inputName = SettingString(settings, "input", "");
			const String operand = SettingString(settings, "operand", "");
			const float constant = SettingFloat(settings, "value", 0.0f);
			MathRule rule;
			rule.op = ParseMathOp(SettingString(settings, "op", "add"));
			rule.min = SettingFloat(settings, "min", 0.0f);
			rule.max = SettingFloat(settings, "max", 1.0f);
			rule.inMin = SettingFloat(settings, "inMin", 0.0f);
			rule.inMax = SettingFloat(settings, "inMax", 1.0f);
			rule.outMin = SettingFloat(settings, "outMin", 0.0f);
			rule.outMax = SettingFloat(settings, "outMax", 1.0f);
			if (rule.op == MathOp::Invalid || target.empty())
			{
				out.error = target.empty() ? "target is empty" : "unknown op '" + SettingString(settings, "op", "") + "'";
				out.pins["Out"];
				return out;
			}
			const String source = inputName.empty() ? target : inputName;
			const bool perAxisScale = target == "scale" && source == "scale";

			return ForEachPointsItem(inputs, std::move(out), [&](NodeOutput& o, DataItem& item)
			{
				// A target column of another type is reported as such before any operand is read.
				if (!IsReservedPointName(target))
				{
					const auto column = item.points.columns.find(target);
					if (column != item.points.columns.end() && column->second.first != AttributeType::Float)
					{
						WarnOnce(o, "target column '" + target + "' is not Float; left unchanged");
						return;
					}
				}
				for (size_t i = 0; i < item.points.points.size(); ++i)
				{
					float b = constant;
					if (rule.Binary() && !operand.empty() && !ReadNumber(o, item, i, operand, b))
					{
						continue;
					}
					if (perAxisScale)
					{
						Vector3& s = item.points.points[i].scale;
						s = Vector3(rule.Apply(o, s.x, b), rule.Apply(o, s.y, b), rule.Apply(o, s.z, b));
						continue;
					}
					float a = 0.0f;
					if (!ReadNumber(o, item, i, source, a))
					{
						continue;
					}
					if (!WritePointNumber(item, i, target, rule.Apply(o, a, b)))
					{
						WarnOnce(o, "target column '" + target + "' is not Float; left unchanged");
					}
				}

				// The column exists even when every point was skipped (created after the loop, so a missing column
				// does not hide an item attribute of the same name from the operands).
				if (!IsReservedPointName(target) && item.points.columns.count(target) == 0)
				{
					item.points.columns[target] = { AttributeType::Float, std::vector<AttributeValue>(item.points.points.size(), AttributeValue(0.0f)) };
				}
			});
		}

		NodeOutput ExecuteCopyAttributes(const EvalContext&, uint64, const AttributeSet& settings, const std::map<String, Collection>& inputs)
		{
			NodeOutput out;
			const float distance = std::max(0.0f, SettingFloat(settings, "distance", 0.0f));
			std::vector<PreparedControl> controls;
			std::vector<String> names = SplitList(SettingString(settings, "attributes", ""));
			const bool allNames = names.empty();
			std::set<String> nameSet;

			if (const Collection* items = FindInput(inputs, "Controls"))
			{
				for (const DataItem& item : *items)
				{
					if (item.kind != ItemKind::Area && item.kind != ItemKind::Spline)
					{
						WarnOnce(out, "skipped controls that are neither areas nor splines");
						continue;
					}
					PreparedControl target;
					if (!PrepareControl(out, item, target))
					{
						continue;
					}
					controls.push_back(std::move(target));
					if (allNames)
					{
						for (const auto& [name, value] : item.attributes)
						{
							nameSet.insert(name);
						}
					}
				}
			}
			if (allNames)
			{
				names.assign(nameSet.begin(), nameSet.end());
			}

			return ForEachPointsItem(inputs, std::move(out), [&](NodeOutput& o, DataItem& item)
			{
				// Columns for every copied name, typed by the first control holding the name.
				const size_t n = item.points.points.size();
				std::vector<uint8> usable(names.size(), 0);
				for (size_t k = 0; k < names.size(); ++k)
				{
					const String& name = names[k];
					if (IsReservedPointName(name))
					{
						usable[k] = 1;
						continue;
					}
					const AttributeValue* first = nullptr;
					for (const PreparedControl& control : controls)
					{
						const auto it = control.item->attributes.find(name);
						if (it != control.item->attributes.end())
						{
							first = &it->second;
							break;
						}
					}
					if (first == nullptr)
					{
						continue;
					}
					const AttributeType type = TypeOf(*first);
					const auto column = item.points.columns.find(name);
					if (column == item.points.columns.end())
					{
						item.points.columns[name] = { type, std::vector<AttributeValue>(n, DefaultValue(type)) };
					}
					else if (column->second.first != type)
					{
						WarnOnce(o, "column '" + name + "' has another type than the control attribute; left unchanged");
						continue;
					}
					usable[k] = 1;
				}

				for (size_t i = 0; i < n; ++i)
				{
					const Vector3& pos = item.points.points[i].position;
					const PreparedControl* source = nullptr;
					for (const PreparedControl& control : controls)
					{
						if (control.polygon && control.polygon->Contains(pos.x, pos.z))
						{
							source = &control;
							break;
						}
					}
					if (source == nullptr && distance > 0.0f)
					{
						float best = distance;
						for (const PreparedControl& control : controls)
						{
							float d = 0.0f;
							if (control.curve->Nearest(pos.x, pos.z, best, d) && (source == nullptr || d < best))
							{
								best = d;
								source = &control;
							}
						}
					}
					if (source == nullptr)
					{
						continue;
					}
					for (size_t k = 0; k < names.size(); ++k)
					{
						if (usable[k] == 0)
						{
							continue;
						}
						const auto it = source->item->attributes.find(names[k]);
						if (it == source->item->attributes.end())
						{
							continue;
						}
						if (IsReservedPointName(names[k]))
						{
							float value = 0.0f;
							if (!ToNumber(it->second, value))
							{
								WarnOnce(o, "attribute '" + names[k] + "' is not numeric and cannot set the point property");
								continue;
							}
							WritePointNumber(item, i, names[k], value);
						}
						else if (!item.points.SetAttribute(i, names[k], it->second))
						{
							WarnOnce(o, "attribute '" + names[k] + "' has different types on the controls; mismatching values skipped");
						}
					}
				}
			});
		}

		PinDef PointsOut()
		{
			return PinDef{ "Out", { ItemKind::Points } };
		}
	}

	void RegisterPointNodes(NodeRegistry& registry)
	{
		{
			NodeTypeDef def;
			def.type = "TransformPoints";
			def.category = "Point";
			def.title = "Transform Points";
			def.inputs.push_back(PointsInputPin());
			def.outputs.push_back(PointsOut());
			def.settings.push_back(SettingDef{ "offsetMin", Vector3(0, 0, 0), 0, 0, {}, "Minimum world offset" });
			def.settings.push_back(SettingDef{ "offsetMax", Vector3(0, 0, 0), 0, 0, {}, "Maximum world offset" });
			def.settings.push_back(SettingDef{ "yawMin", 0.0f, -360.0f, 360.0f, {}, "Minimum yaw (degrees, about +Y)" });
			def.settings.push_back(SettingDef{ "yawMax", 0.0f, -360.0f, 360.0f, {}, "Maximum yaw (degrees, about +Y)" });
			def.settings.push_back(SettingDef{ "pitchMin", 0.0f, -180.0f, 180.0f, {}, "Minimum pitch (degrees, about +X)" });
			def.settings.push_back(SettingDef{ "pitchMax", 0.0f, -180.0f, 180.0f, {}, "Maximum pitch (degrees, about +X)" });
			def.settings.push_back(SettingDef{ "rollMin", 0.0f, -180.0f, 180.0f, {}, "Minimum roll (degrees, about +Z)" });
			def.settings.push_back(SettingDef{ "rollMax", 0.0f, -180.0f, 180.0f, {}, "Maximum roll (degrees, about +Z)" });
			def.settings.push_back(SettingDef{ "scaleMin", Vector3(1, 1, 1), 0, 0, {}, "Minimum scale factor" });
			def.settings.push_back(SettingDef{ "scaleMax", Vector3(1, 1, 1), 0, 0, {}, "Maximum scale factor" });
			def.settings.push_back(SettingDef{ "uniformScale", true, 0, 0, {}, "Scale all axes by one factor (x range)" });
			def.settings.push_back(SettingDef{ "orientation", String("keep"), 0, 0, { "keep", "upright", "alignToTerrain" }, "Rotation the random rotation starts from" });
			def.contextDeps = context_dep::Terrain;
			def.execute = ExecuteTransformPoints;
			registry.Register(std::move(def));
		}
		{
			NodeTypeDef def;
			def.type = "DensityNoise";
			def.category = "Point";
			def.title = "Density Noise";
			def.inputs.push_back(PointsInputPin());
			def.outputs.push_back(PointsOut());
			def.settings.push_back(SettingDef{ "frequency", 0.05f, 0.0001f, 10.0f, {}, "Noise frequency (1 / metres)" });
			def.settings.push_back(SettingDef{ "octaves", static_cast<int64>(3), 1.0f, 8.0f, {}, "Noise octaves" });
			def.settings.push_back(SettingDef{ "persistence", 0.5f, 0.0f, 1.0f, {}, "Amplitude factor per octave" });
			def.settings.push_back(SettingDef{ "amount", 1.0f, 0.0f, 1.0f, {}, "Strength of the noise" });
			def.settings.push_back(SettingDef{ "mode", String("multiply"), 0, 0, { "multiply", "add", "set" }, "How the noise changes the density" });
			def.execute = ExecuteDensityNoise;
			registry.Register(std::move(def));
		}
		{
			NodeTypeDef def;
			def.type = "AttributeMath";
			def.category = "Point";
			def.title = "Attribute Math";
			def.inputs.push_back(PointsInputPin());
			def.outputs.push_back(PointsOut());
			def.settings.push_back(SettingDef{ "target", String("density"), 0, 0, {}, "Attribute or point property written (density, scale, position.y, ...)" });
			def.settings.push_back(SettingDef{ "input", String(), 0, 0, {}, "Operand A (empty = the target)" });
			def.settings.push_back(SettingDef{ "op", String("add"), 0, 0, { "add", "sub", "mul", "div", "min", "max", "clamp", "remap" }, "Operation" });
			def.settings.push_back(SettingDef{ "operand", String(), 0, 0, {}, "Attribute for operand B (empty = value)" });
			def.settings.push_back(SettingDef{ "value", 0.0f, 0, 0, {}, "Constant operand B" });
			def.settings.push_back(SettingDef{ "min", 0.0f, 0, 0, {}, "clamp: lower bound" });
			def.settings.push_back(SettingDef{ "max", 1.0f, 0, 0, {}, "clamp: upper bound" });
			def.settings.push_back(SettingDef{ "inMin", 0.0f, 0, 0, {}, "remap: input range start" });
			def.settings.push_back(SettingDef{ "inMax", 1.0f, 0, 0, {}, "remap: input range end" });
			def.settings.push_back(SettingDef{ "outMin", 0.0f, 0, 0, {}, "remap: output range start" });
			def.settings.push_back(SettingDef{ "outMax", 1.0f, 0, 0, {}, "remap: output range end" });
			def.execute = ExecuteAttributeMath;
			registry.Register(std::move(def));
		}
		{
			NodeTypeDef def;
			def.type = "CopyAttributesFromControl";
			def.category = "Point";
			def.title = "Copy Attributes from Control";
			def.inputs.push_back(PointsInputPin());
			def.inputs.push_back(PinDef{ "Controls", { ItemKind::Area, ItemKind::Spline }, true, true });
			def.outputs.push_back(PointsOut());
			def.settings.push_back(SettingDef{ "attributes", String(), 0, 0, {}, "Comma list of item attributes to copy (empty = all)" });
			def.settings.push_back(SettingDef{ "distance", 0.0f, 0.0f, 1000.0f, {}, "Search distance for splines and area edges (0 = containing areas only)" });
			def.execute = ExecuteCopyAttributes;
			registry.Register(std::move(def));
		}
	}
}
