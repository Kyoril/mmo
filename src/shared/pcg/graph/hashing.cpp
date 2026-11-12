// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "hashing.h"

namespace mmo::pcg::graph
{
	void Hasher::Value(const AttributeValue& v)
	{
		U64(v.index());
		switch (TypeOf(v))
		{
		case AttributeType::Float:
			F32(std::get<float>(v));
			break;
		case AttributeType::Int:
			U64(static_cast<uint64>(std::get<int64>(v)));
			break;
		case AttributeType::Bool:
			U64(std::get<bool>(v) ? 1 : 0);
			break;
		case AttributeType::Vector3:
			Vec(std::get<Vector3>(v));
			break;
		case AttributeType::String:
			Str(std::get<String>(v));
			break;
		}
	}

	void Hasher::Attributes(const AttributeSet& set)
	{
		U64(set.size());
		for (const auto& entry : set)
		{
			Str(entry.first);
			Value(entry.second);
		}
	}

	void Hasher::Spline(const PathSpline& spline)
	{
		U64(spline.closed ? 1 : 0);
		U64(static_cast<uint64>(spline.interpolation));
		U64(spline.points.size());
		for (const SplinePoint& p : spline.points)
		{
			F32(p.x);
			F32(p.z);
			F32(p.width);
			F32(p.y);
			U64(p.autoTangents ? 1 : 0);
			F32(p.inX);
			F32(p.inZ);
			F32(p.outX);
			F32(p.outZ);
			F32(p.inY);
			F32(p.outY);
		}
	}

	uint64 StringHash(const String& s)
	{
		uint64 h = 14695981039346656037ull;
		for (const char c : s)
		{
			h ^= static_cast<uint8>(c);
			h *= 1099511628211ull;
		}
		return h;
	}

	uint64 HashAttributes(const AttributeSet& attributes)
	{
		Hasher h;
		h.Attributes(attributes);
		return h.Result();
	}

	uint64 HashSpline(const PathSpline& spline)
	{
		Hasher h;
		h.Spline(spline);
		return h.Result();
	}
}
