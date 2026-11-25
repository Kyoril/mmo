// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "controls.h"
#include "hashing.h"
#include "json_util.h"

#include <algorithm>
#include <cmath>

namespace mmo::pcg::graph
{
	namespace
	{
		using Json = nlohmann::json;
		using namespace jsonutil;

		constexpr int kFileVersion = 1;

		struct NameEntry
		{
			const char* name;
			int value;
		};

		const NameEntry kShapeNames[] = {
			{ "closedLoop", static_cast<int>(ControlShape::ClosedLoop) },
			{ "openSpline", static_cast<int>(ControlShape::OpenSpline) },
			{ "volume", static_cast<int>(ControlShape::Volume) },
			{ "point", static_cast<int>(ControlShape::Point) },
			{ "rectangle", static_cast<int>(ControlShape::Rectangle) }
		};

		const NameEntry kPropertyTypeNames[] = {
			{ "float", static_cast<int>(PropertyType::Float) },
			{ "int", static_cast<int>(PropertyType::Int) },
			{ "bool", static_cast<int>(PropertyType::Bool) },
			{ "enum", static_cast<int>(PropertyType::Enum) },
			{ "string", static_cast<int>(PropertyType::String) },
			{ "asset", static_cast<int>(PropertyType::Asset) },
			{ "assetList", static_cast<int>(PropertyType::AssetList) },
			{ "color", static_cast<int>(PropertyType::Color) }
		};

		const NameEntry kInterpolationNames[] = {
			{ "linear", static_cast<int>(SplineInterpolation::Linear) },
			{ "catmullRom", static_cast<int>(SplineInterpolation::CatmullRom) }
		};

		template <size_t N>
		const char* NameOf(const NameEntry (&table)[N], const int value)
		{
			for (const NameEntry& e : table)
			{
				if (e.value == value)
				{
					return e.name;
				}
			}
			return table[0].name;
		}

		template <size_t N>
		bool ValueOfName(const NameEntry (&table)[N], const String& name, int& out)
		{
			for (const NameEntry& e : table)
			{
				if (name == e.name)
				{
					out = e.value;
					return true;
				}
			}
			return false;
		}

		bool ReadUInt64(const Json& obj, const char* key, uint64& out, const String& context, String& error, const bool required)
		{
			const Json* v = Find(obj, key);
			if (v == nullptr)
			{
				if (required)
				{
					error = context + ": missing '" + key + "'";
					return false;
				}
				return true;
			}
			if (!v->is_number_integer() || (v->is_number_integer() && !v->is_number_unsigned() && v->get<int64_t>() < 0))
			{
				error = context + ": '" + key + "' must be a non-negative integer";
				return false;
			}
			out = v->get<uint64_t>();
			return true;
		}

		bool ReadBool(const Json& obj, const char* key, bool& out, const String& context, String& error)
		{
			const Json* v = Find(obj, key);
			if (v == nullptr)
			{
				return true;
			}
			if (!v->is_boolean())
			{
				error = context + ": '" + key + "' must be a boolean";
				return false;
			}
			out = v->get<bool>();
			return true;
		}

		bool ReadNumbers(const Json& obj, const char* key, float* out, const size_t count, const String& context, String& error)
		{
			const Json* v = Find(obj, key);
			if (v == nullptr)
			{
				return true;
			}
			if (!v->is_array() || v->size() != count)
			{
				error = context + ": '" + key + "' must be an array of " + std::to_string(count) + " numbers";
				return false;
			}
			float tmp[4] = {};
			for (size_t i = 0; i < count; ++i)
			{
				if (!FloatFromJson((*v)[i], tmp[i]))
				{
					error = context + ": '" + key + "' needs finite numbers within float range";
					return false;
				}
			}
			for (size_t i = 0; i < count; ++i)
			{
				out[i] = tmp[i];
			}
			return true;
		}

		bool ReadStringSet(const Json& obj, const char* key, std::set<String>& out, const String& context, String& error)
		{
			const Json* v = Find(obj, key);
			if (v == nullptr)
			{
				return true;
			}
			if (!v->is_array())
			{
				error = context + ": '" + key + "' must be an array of strings";
				return false;
			}
			for (const Json& e : *v)
			{
				if (!e.is_string())
				{
					error = context + ": '" + key + "' must be an array of strings";
					return false;
				}
				out.insert(e.get<String>());
			}
			return true;
		}

		Json StringSetToJson(const std::set<String>& set)
		{
			Json a = Json::array();
			for (const String& s : set)
			{
				a.push_back(s);
			}
			return a;
		}

		bool IsFinite(const AttributeValue& v)
		{
			switch (TypeOf(v))
			{
			case AttributeType::Float: return std::isfinite(std::get<float>(v));
			case AttributeType::Vector3:
			{
				const Vector3& vec = std::get<Vector3>(v);
				return std::isfinite(vec.x) && std::isfinite(vec.y) && std::isfinite(vec.z);
			}
			default: return true;
			}
		}

		// ---- control types ----

		Json PropertyToJson(const PropertyDef& p)
		{
			Json j = Json::object();
			j["name"] = p.name;
			j["type"] = NameOf(kPropertyTypeNames, static_cast<int>(p.type));
			j["default"] = ValueToJson(p.defaultValue);
			j["min"] = SafeDouble(p.min);
			j["max"] = SafeDouble(p.max);
			Json values = Json::array();
			for (const String& v : p.enumValues)
			{
				values.push_back(v);
			}
			j["enumValues"] = std::move(values);
			j["description"] = p.description;
			return j;
		}

		bool ParseProperty(const Json& j, const String& context, PropertyDef& p, String& error)
		{
			if (!j.is_object())
			{
				error = context + ": must be an object";
				return false;
			}
			String typeName;
			if (!ReadString(j, "name", p.name, context, error, true) || !ReadString(j, "type", typeName, context, error, true) ||
				!ReadString(j, "description", p.description, context, error, false))
			{
				return false;
			}
			if (p.name.empty())
			{
				error = context + ": property name must not be empty";
				return false;
			}
			int type = 0;
			if (!ValueOfName(kPropertyTypeNames, typeName, type))
			{
				error = context + " '" + p.name + "': unknown property type '" + typeName + "'";
				return false;
			}
			p.type = static_cast<PropertyType>(type);

			const Json* def = Find(j, "default");
			if (def == nullptr)
			{
				error = context + " '" + p.name + "': missing 'default'";
				return false;
			}
			String valueError;
			if (!ValueFromJson(*def, p.defaultValue, valueError))
			{
				error = context + " '" + p.name + "': " + valueError;
				return false;
			}
			if (TypeOf(p.defaultValue) != AttributeTypeOf(p.type))
			{
				error = context + " '" + p.name + "': default value has the wrong type for property type '" + typeName + "'";
				return false;
			}

			if (!ReadNumber(j, "min", p.min, context + " '" + p.name + "'", error) || !ReadNumber(j, "max", p.max, context + " '" + p.name + "'", error))
			{
				return false;
			}

			if (const Json* values = Find(j, "enumValues"))
			{
				if (!values->is_array())
				{
					error = context + " '" + p.name + "': 'enumValues' must be an array of strings";
					return false;
				}
				for (const Json& v : *values)
				{
					if (!v.is_string())
					{
						error = context + " '" + p.name + "': 'enumValues' must be an array of strings";
						return false;
					}
					p.enumValues.push_back(v.get<String>());
				}
			}
			return true;
		}

		// ---- instances ----

		Json SplineToJson(const PathSpline& s)
		{
			Json j = Json::object();
			j["closed"] = s.closed;
			j["interpolation"] = NameOf(kInterpolationNames, static_cast<int>(s.interpolation));
			Json points = Json::array();
			for (const SplinePoint& p : s.points)
			{
				Json pj = Json::object();
				pj["x"] = SafeDouble(p.x);
				pj["y"] = SafeDouble(p.y);
				pj["z"] = SafeDouble(p.z);
				pj["width"] = SafeDouble(p.width);
				pj["autoTangents"] = p.autoTangents;
				pj["inX"] = SafeDouble(p.inX);
				pj["inY"] = SafeDouble(p.inY);
				pj["inZ"] = SafeDouble(p.inZ);
				pj["outX"] = SafeDouble(p.outX);
				pj["outY"] = SafeDouble(p.outY);
				pj["outZ"] = SafeDouble(p.outZ);
				points.push_back(std::move(pj));
			}
			j["points"] = std::move(points);
			return j;
		}

		bool ParseSpline(const Json& j, PathSpline& out, String& error)
		{
			const String context = "spline";
			if (!j.is_object())
			{
				error = "spline: must be an object";
				return false;
			}
			PathSpline s;
			if (!ReadBool(j, "closed", s.closed, context, error))
			{
				return false;
			}
			String interp;
			if (!ReadString(j, "interpolation", interp, context, error, false))
			{
				return false;
			}
			if (!interp.empty())
			{
				int v = 0;
				if (!ValueOfName(kInterpolationNames, interp, v))
				{
					error = "spline: unknown interpolation '" + interp + "'";
					return false;
				}
				s.interpolation = static_cast<SplineInterpolation>(v);
			}
			if (const Json* points = Find(j, "points"))
			{
				if (!points->is_array())
				{
					error = "spline: 'points' must be an array";
					return false;
				}
				for (size_t i = 0; i < points->size(); ++i)
				{
					const String pctx = "spline.points[" + std::to_string(i) + "]";
					const Json& pj = (*points)[i];
					if (!pj.is_object())
					{
						error = pctx + ": must be an object";
						return false;
					}
					SplinePoint p;
					if (!ReadNumber(pj, "x", p.x, pctx, error) || !ReadNumber(pj, "y", p.y, pctx, error) || !ReadNumber(pj, "z", p.z, pctx, error) ||
						!ReadNumber(pj, "width", p.width, pctx, error) || !ReadBool(pj, "autoTangents", p.autoTangents, pctx, error) ||
						!ReadNumber(pj, "inX", p.inX, pctx, error) || !ReadNumber(pj, "inY", p.inY, pctx, error) || !ReadNumber(pj, "inZ", p.inZ, pctx, error) ||
						!ReadNumber(pj, "outX", p.outX, pctx, error) || !ReadNumber(pj, "outY", p.outY, pctx, error) || !ReadNumber(pj, "outZ", p.outZ, pctx, error))
					{
						return false;
					}
					s.points.push_back(p);
				}
			}
			out = std::move(s);
			return true;
		}

		// ---- hashing ----

		void HashType(Hasher& h, const ControlType& t)
		{
			h.Str(t.name);
			h.U64(static_cast<uint64>(t.shape));
			h.Tags(t.defaultTags);
			h.U64(t.groups.size());
			for (const PropertyGroup& g : t.groups)
			{
				h.Str(g.name);
				h.U64(g.properties.size());
				for (const PropertyDef& p : g.properties)
				{
					h.Str(p.name);
					h.U64(static_cast<uint64>(p.type));
					h.Value(p.defaultValue);
					h.F32(p.min);
					h.F32(p.max);
					h.U64(p.enumValues.size());
					for (const String& e : p.enumValues)
					{
						h.Str(e);
					}
					h.Str(p.description);
				}
			}
		}

		void HashInstance(Hasher& h, const ControlInstance& i)
		{
			h.U64(i.id);
			h.Str(i.typePath);
			h.Str(i.name);
			h.Tags(i.extraTags);

			h.Spline(i.spline);
			h.U64(i.pointIds.size());
			for (const uint64 id : i.pointIds)
			{
				h.U64(id);
			}
			h.U64(i.nextPointId);

			h.Vec(i.position);
			h.Quat(i.rotation);
			h.Vec(i.halfExtents);
			h.Attributes(i.overrides);
		}
	}

	AttributeType AttributeTypeOf(const PropertyType type)
	{
		switch (type)
		{
		case PropertyType::Float: return AttributeType::Float;
		case PropertyType::Int: return AttributeType::Int;
		case PropertyType::Bool: return AttributeType::Bool;
		case PropertyType::Color: return AttributeType::Vector3;
		case PropertyType::Enum:
		case PropertyType::String:
		case PropertyType::Asset:
		case PropertyType::AssetList: return AttributeType::String;
		}
		return AttributeType::String;
	}

	bool ParseControlType(const String& json, ControlType& out, String& error)
	{
		const Json root = Json::parse(json, nullptr, false);
		if (root.is_discarded())
		{
			error = "malformed JSON";
			return false;
		}
		if (!root.is_object())
		{
			error = "control type file must be a JSON object";
			return false;
		}
		const Json* version = Find(root, "version");
		if (version == nullptr || !IsInt64(*version) || version->get<int64_t>() != kFileVersion)
		{
			error = "unsupported or missing control type file version (expected 1)";
			return false;
		}

		ControlType type;
		String shapeName;
		if (!ReadString(root, "name", type.name, "control type", error, false) || !ReadString(root, "shape", shapeName, "control type", error, false) ||
			!ReadStringSet(root, "defaultTags", type.defaultTags, "control type", error))
		{
			return false;
		}
		if (!shapeName.empty())
		{
			int shape = 0;
			if (!ValueOfName(kShapeNames, shapeName, shape))
			{
				error = "unknown control shape '" + shapeName + "'";
				return false;
			}
			type.shape = static_cast<ControlShape>(shape);
		}

		std::set<String> names;
		if (const Json* groups = Find(root, "groups"))
		{
			if (!groups->is_array())
			{
				error = "'groups' must be an array";
				return false;
			}
			for (size_t gi = 0; gi < groups->size(); ++gi)
			{
				const String gctx = "groups[" + std::to_string(gi) + "]";
				const Json& gj = (*groups)[gi];
				if (!gj.is_object())
				{
					error = gctx + ": must be an object";
					return false;
				}
				PropertyGroup group;
				if (!ReadString(gj, "name", group.name, gctx, error, false))
				{
					return false;
				}
				if (const Json* props = Find(gj, "properties"))
				{
					if (!props->is_array())
					{
						error = gctx + ": 'properties' must be an array";
						return false;
					}
					for (size_t pi = 0; pi < props->size(); ++pi)
					{
						PropertyDef def;
						if (!ParseProperty((*props)[pi], gctx + ".properties[" + std::to_string(pi) + "]", def, error))
						{
							return false;
						}
						if (!names.insert(def.name).second)
						{
							error = gctx + ": duplicate property name '" + def.name + "'";
							return false;
						}
						group.properties.push_back(std::move(def));
					}
				}
				type.groups.push_back(std::move(group));
			}
		}

		out = std::move(type);
		return true;
	}

	String WriteControlType(const ControlType& type)
	{
		Json root = Json::object();
		root["version"] = kFileVersion;
		root["name"] = type.name;
		root["shape"] = NameOf(kShapeNames, static_cast<int>(type.shape));
		root["defaultTags"] = StringSetToJson(type.defaultTags);
		Json groups = Json::array();
		for (const PropertyGroup& g : type.groups)
		{
			Json gj = Json::object();
			gj["name"] = g.name;
			Json props = Json::array();
			for (const PropertyDef& p : g.properties)
			{
				props.push_back(PropertyToJson(p));
			}
			gj["properties"] = std::move(props);
			groups.push_back(std::move(gj));
		}
		root["groups"] = std::move(groups);
		return Dump(root);
	}

	bool ParseControlInstance(const String& json, ControlInstance& out, String& error)
	{
		const Json root = Json::parse(json, nullptr, false);
		if (root.is_discarded())
		{
			error = "malformed JSON";
			return false;
		}
		if (!root.is_object())
		{
			error = "control file must be a JSON object";
			return false;
		}
		const Json* version = Find(root, "version");
		if (version == nullptr || !IsInt64(*version) || version->get<int64_t>() != kFileVersion)
		{
			error = "unsupported or missing control file version (expected 1)";
			return false;
		}

		const String context = "control";
		ControlInstance inst;
		if (!ReadUInt64(root, "id", inst.id, context, error, false) || !ReadString(root, "typePath", inst.typePath, context, error, false) ||
			!ReadString(root, "name", inst.name, context, error, false) || !ReadStringSet(root, "extraTags", inst.extraTags, context, error) ||
			!ReadUInt64(root, "nextPointId", inst.nextPointId, context, error, false))
		{
			return false;
		}

		if (const Json* spline = Find(root, "spline"))
		{
			if (!ParseSpline(*spline, inst.spline, error))
			{
				return false;
			}
		}

		if (const Json* ids = Find(root, "pointIds"))
		{
			if (!ids->is_array())
			{
				error = "'pointIds' must be an array";
				return false;
			}
			std::set<uint64> seen;
			for (const Json& e : *ids)
			{
				if (!e.is_number_integer() || (!e.is_number_unsigned() && e.get<int64_t>() < 0))
				{
					error = "'pointIds' must hold non-negative integers";
					return false;
				}
				const uint64 id = e.get<uint64_t>();
				if (id == 0 || !seen.insert(id).second)
				{
					error = "'pointIds' must be unique and non-zero";
					return false;
				}
				inst.pointIds.push_back(id);
			}
		}
		if (inst.pointIds.size() != inst.spline.points.size())
		{
			error = "'pointIds' needs exactly one id per spline point";
			return false;
		}
		for (const uint64 id : inst.pointIds)
		{
			if (id == UINT64_MAX)
			{
				error = "'pointIds' contains an id that cannot be advanced";
				return false;
			}
			inst.nextPointId = std::max(inst.nextPointId, id + 1);
		}
		inst.nextPointId = std::max<uint64>(inst.nextPointId, 1);

		float v3[3] = { inst.position.x, inst.position.y, inst.position.z };
		if (!ReadNumbers(root, "position", v3, 3, context, error))
		{
			return false;
		}
		inst.position = Vector3(v3[0], v3[1], v3[2]);
		float q[4] = { inst.rotation.w, inst.rotation.x, inst.rotation.y, inst.rotation.z };
		if (!ReadNumbers(root, "rotation", q, 4, context, error))
		{
			return false;
		}
		inst.rotation = Quaternion(q[0], q[1], q[2], q[3]);
		float he[3] = { inst.halfExtents.x, inst.halfExtents.y, inst.halfExtents.z };
		if (!ReadNumbers(root, "halfExtents", he, 3, context, error))
		{
			return false;
		}
		inst.halfExtents = Vector3(he[0], he[1], he[2]);

		if (const Json* overrides = Find(root, "overrides"))
		{
			if (!overrides->is_object())
			{
				error = "'overrides' must be an object";
				return false;
			}
			for (auto it = overrides->begin(); it != overrides->end(); ++it)
			{
				AttributeValue value;
				String valueError;
				if (!ValueFromJson(it.value(), value, valueError))
				{
					error = "override '" + it.key() + "': " + valueError;
					return false;
				}
				inst.overrides[it.key()] = value;
			}
		}

		out = std::move(inst);
		return true;
	}

	String WriteControlInstance(const ControlInstance& inst)
	{
		Json root = Json::object();
		root["version"] = kFileVersion;
		root["id"] = inst.id;
		root["typePath"] = inst.typePath;
		root["name"] = inst.name;
		root["extraTags"] = StringSetToJson(inst.extraTags);
		root["spline"] = SplineToJson(inst.spline);
		Json ids = Json::array();
		for (const uint64 id : inst.pointIds)
		{
			ids.push_back(id);
		}
		root["pointIds"] = std::move(ids);
		root["nextPointId"] = inst.nextPointId;
		root["position"] = Json::array({ SafeDouble(inst.position.x), SafeDouble(inst.position.y), SafeDouble(inst.position.z) });
		root["rotation"] = Json::array({ SafeDouble(inst.rotation.w), SafeDouble(inst.rotation.x), SafeDouble(inst.rotation.y), SafeDouble(inst.rotation.z) });
		root["halfExtents"] = Json::array({ SafeDouble(inst.halfExtents.x), SafeDouble(inst.halfExtents.y), SafeDouble(inst.halfExtents.z) });
		Json overrides = Json::object();
		for (const auto& entry : inst.overrides)
		{
			overrides[entry.first] = ValueToJson(entry.second);
		}
		root["overrides"] = std::move(overrides);
		return Dump(root);
	}

	bool ResolveControl(const ControlType& type, const ControlInstance& inst, ResolvedControl& out, std::vector<String>& warnings)
	{
		const String who = "control '" + inst.name + "' (" + std::to_string(inst.id) + ")";

		// Shape consistency
		const bool splineShape = type.shape == ControlShape::ClosedLoop || type.shape == ControlShape::OpenSpline;
		if (splineShape)
		{
			const bool loop = type.shape == ControlShape::ClosedLoop;
			const size_t minPoints = loop ? 3 : 2;
			if (inst.spline.closed != loop)
			{
				warnings.push_back(who + ": type '" + type.name + "' needs a " + (loop ? "closed" : "open") + " spline");
				return false;
			}
			if (inst.spline.points.size() < minPoints)
			{
				warnings.push_back(who + ": type '" + type.name + "' needs at least " + std::to_string(minPoints) + " spline points, has " + std::to_string(inst.spline.points.size()));
				return false;
			}
			if (inst.pointIds.size() != inst.spline.points.size())
			{
				warnings.push_back(who + ": has " + std::to_string(inst.pointIds.size()) + " point ids for " + std::to_string(inst.spline.points.size()) + " spline points");
				return false;
			}
		}

		ResolvedControl resolved;
		resolved.type = &type;
		resolved.instance = &inst;
		resolved.tags = type.defaultTags;
		resolved.tags.insert(inst.extraTags.begin(), inst.extraTags.end());

		std::set<String> known;
		for (const PropertyGroup& group : type.groups)
		{
			for (const PropertyDef& prop : group.properties)
			{
				known.insert(prop.name);
				const AttributeType expected = AttributeTypeOf(prop.type);

				AttributeValue value = prop.defaultValue;
				const auto it = inst.overrides.find(prop.name);
				if (it != inst.overrides.end())
				{
					if (TypeOf(it->second) != expected)
					{
						warnings.push_back(who + ": override '" + prop.name + "' has the wrong type; using the default");
					}
					else if (!IsFinite(it->second))
					{
						warnings.push_back(who + ": override '" + prop.name + "' is not finite; using the default");
					}
					else if (prop.type == PropertyType::Enum && std::find(prop.enumValues.begin(), prop.enumValues.end(), std::get<String>(it->second)) == prop.enumValues.end())
					{
						warnings.push_back(who + ": override '" + prop.name + "' is not one of the allowed values; using the default");
					}
					else
					{
						value = it->second;
					}
				}

				if (prop.max > prop.min)
				{
					if (prop.type == PropertyType::Float && TypeOf(value) == AttributeType::Float)
					{
						const float f = std::get<float>(value);
						const float c = std::min(std::max(f, prop.min), prop.max);
						if (c != f && it != inst.overrides.end())
						{
							warnings.push_back(who + ": override '" + prop.name + "' clamped to [" + std::to_string(prop.min) + ", " + std::to_string(prop.max) + "]");
						}
						value = c;
					}
					else if (prop.type == PropertyType::Int && TypeOf(value) == AttributeType::Int)
					{
						const int64 i = std::get<int64>(value);
						const int64 lo = static_cast<int64>(std::ceil(prop.min));
						const int64 hi = static_cast<int64>(std::floor(prop.max));
						const int64 c = hi < lo ? lo : std::min(std::max(i, lo), hi);
						if (c != i && it != inst.overrides.end())
						{
							warnings.push_back(who + ": override '" + prop.name + "' clamped to [" + std::to_string(lo) + ", " + std::to_string(hi) + "]");
						}
						value = c;
					}
				}

				resolved.attributes[prop.name] = value;
			}
		}

		for (const auto& entry : inst.overrides)
		{
			if (known.find(entry.first) == known.end())
			{
				warnings.push_back(who + ": override '" + entry.first + "' is not a property of type '" + type.name + "'; kept as is");
				resolved.attributes[entry.first] = entry.second;
			}
		}

		out = std::move(resolved);
		return true;
	}

	uint64 ControlsHash(const std::vector<ResolvedControl>& controls)
	{
		Hasher h;
		h.U64(controls.size());
		for (const ResolvedControl& c : controls)
		{
			h.U64(c.type != nullptr ? 1 : 0);
			if (c.type != nullptr)
			{
				HashType(h, *c.type);
			}
			h.U64(c.instance != nullptr ? 1 : 0);
			if (c.instance != nullptr)
			{
				HashInstance(h, *c.instance);
			}
			h.Tags(c.tags);
			h.Attributes(c.attributes);
		}
		return h.Result();
	}
}
