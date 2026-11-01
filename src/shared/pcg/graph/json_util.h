// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "graph_data.h"

#include "nlohmann/json.hpp"

#include <cfloat>
#include <cmath>
#include <cstdint>

/// @brief Shared helpers for the graph file formats: exception-free parsing, strict number reading
///        (finite, in range), typed attribute values and deterministic dumping.
namespace mmo::pcg::graph::jsonutil
{
	using Json = nlohmann::json;


	inline const char* TypeName(const AttributeType type)
	{
		switch (type)
		{
		case AttributeType::Float: return "float";
		case AttributeType::Int: return "int";
		case AttributeType::Bool: return "bool";
		case AttributeType::Vector3: return "vector3";
		case AttributeType::String: return "string";
		}
		return "float";
	}

	inline bool TypeFromName(const String& name, AttributeType& out)
	{
		for (const AttributeType t : { AttributeType::Float, AttributeType::Int, AttributeType::Bool, AttributeType::Vector3, AttributeType::String })
		{
			if (name == TypeName(t))
			{
				out = t;
				return true;
			}
		}
		return false;
	}

	/// Non-finite floats cannot be represented in JSON (they would dump as null), so they are written as 0.
	inline double SafeDouble(const float v)
	{
		return std::isfinite(v) ? static_cast<double>(v) : 0.0;
	}

	/// Reads a finite number that fits into a float.
	inline bool FloatFromJson(const Json& j, float& out)
	{
		if (!j.is_number())
		{
			return false;
		}
		const double d = j.get<double>();
		if (!std::isfinite(d) || std::fabs(d) > static_cast<double>(FLT_MAX))
		{
			return false;
		}
		out = static_cast<float>(d);
		return true;
	}

	/// Whether the number is an integer that fits int64 (unsigned values above INT64_MAX do not).
	inline bool IsInt64(const Json& j)
	{
		return j.is_number_integer() && !(j.is_number_unsigned() && j.get<uint64_t>() > static_cast<uint64_t>(INT64_MAX));
	}

	/// Dumps with invalid UTF-8 replaced instead of throwing.
	inline String Dump(const Json& j)
	{
		return j.dump(2, ' ', false, Json::error_handler_t::replace);
	}

	inline Json ValueToJson(const AttributeValue& v)
	{
		Json j = Json::object();
		j["type"] = TypeName(TypeOf(v));
		switch (TypeOf(v))
		{
		case AttributeType::Float: j["value"] = SafeDouble(std::get<float>(v)); break;
		case AttributeType::Int: j["value"] = std::get<int64>(v); break;
		case AttributeType::Bool: j["value"] = std::get<bool>(v); break;
		case AttributeType::Vector3:
		{
			const Vector3& vec = std::get<Vector3>(v);
			j["value"] = Json::array({ SafeDouble(vec.x), SafeDouble(vec.y), SafeDouble(vec.z) });
			break;
		}
		case AttributeType::String: j["value"] = std::get<String>(v); break;
		}
		return j;
	}

	inline const Json* Find(const Json& obj, const char* key)
	{
		const auto it = obj.find(key);
		return it == obj.end() ? nullptr : &*it;
	}

	inline bool ValueFromJson(const Json& j, AttributeValue& out, String& error)
	{
		if (!j.is_object())
		{
			error = "attribute value must be an object";
			return false;
		}

		const Json* type = Find(j, "type");
		const Json* value = Find(j, "value");
		if (type == nullptr || !type->is_string() || value == nullptr)
		{
			error = "attribute value needs a string 'type' and a 'value'";
			return false;
		}

		AttributeType t;
		if (!TypeFromName(type->get<String>(), t))
		{
			error = "unknown attribute type '" + type->get<String>() + "'";
			return false;
		}

		switch (t)
		{
		case AttributeType::Float:
		{
			float f = 0.0f;
			if (!FloatFromJson(*value, f))
			{
				error = "float attribute needs a finite number within float range";
				return false;
			}
			out = f;
			return true;
		}
		case AttributeType::Int:
			if (!IsInt64(*value))
			{
				error = "int attribute needs an integer within int64 range";
				return false;
			}
			out = static_cast<int64>(value->get<int64_t>());
			return true;
		case AttributeType::Bool:
			if (!value->is_boolean())
			{
				error = "bool attribute needs a boolean";
				return false;
			}
			out = value->get<bool>();
			return true;
		case AttributeType::Vector3:
		{
			float c[3] = { 0.0f, 0.0f, 0.0f };
			if (!value->is_array() || value->size() != 3 || !FloatFromJson((*value)[0], c[0]) || !FloatFromJson((*value)[1], c[1]) || !FloatFromJson((*value)[2], c[2]))
			{
				error = "vector3 attribute needs an array of three finite numbers within float range";
				return false;
			}
			out = Vector3(c[0], c[1], c[2]);
			return true;
		}
		case AttributeType::String:
			if (!value->is_string())
			{
				error = "string attribute needs a string";
				return false;
			}
			out = value->get<String>();
			return true;
		}
		return false;
	}

	inline bool ReadString(const Json& obj, const char* key, String& out, const String& context, String& error, const bool required)
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
		if (!v->is_string())
		{
			error = context + ": '" + key + "' must be a string";
			return false;
		}
		out = v->get<String>();
		return true;
	}

	inline bool ReadUInt(const Json& obj, const char* key, uint32& out, const String& context, String& error, const bool required)
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
		if (!IsInt64(*v) || v->get<int64_t>() < 0 || v->get<int64_t>() > 0xFFFFFFFFll)
		{
			error = context + ": '" + key + "' must be a non-negative 32 bit integer";
			return false;
		}
		out = static_cast<uint32>(v->get<int64_t>());
		return true;
	}

	inline bool ReadNumber(const Json& obj, const char* key, float& out, const String& context, String& error)
	{
		const Json* v = Find(obj, key);
		if (v == nullptr)
		{
			return true;
		}
		if (!FloatFromJson(*v, out))
		{
			error = context + ": '" + key + "' must be a finite number within float range";
			return false;
		}
		return true;
	}

}
