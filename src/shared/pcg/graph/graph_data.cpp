// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "graph_data.h"

#include <cstring>

namespace mmo::pcg::graph
{
	AttributeType TypeOf(const AttributeValue& v)
	{
		return static_cast<AttributeType>(v.index());
	}

	AttributeValue DefaultValue(const AttributeType type)
	{
		switch (type)
		{
		case AttributeType::Float: return AttributeValue(0.0f);
		case AttributeType::Int: return AttributeValue(static_cast<int64>(0));
		case AttributeType::Bool: return AttributeValue(false);
		case AttributeType::Vector3: return AttributeValue(Vector3());
		case AttributeType::String: return AttributeValue(String());
		}
		return AttributeValue(0.0f);
	}

	bool PointSet::SetAttribute(const size_t index, const String& name, const AttributeValue& value)
	{
		if (index >= points.size())
		{
			return false;
		}

		const AttributeType type = TypeOf(value);
		auto it = columns.find(name);
		if (it == columns.end())
		{
			auto& column = columns[name];
			column.first = type;
			column.second.assign(points.size(), DefaultValue(type));
			column.second[index] = value;
			return true;
		}

		if (it->second.first != type)
		{
			return false;
		}

		if (it->second.second.size() < points.size())
		{
			it->second.second.resize(points.size(), DefaultValue(type));
		}
		it->second.second[index] = value;
		return true;
	}

	const AttributeValue* PointSet::GetAttribute(const size_t index, const String& name) const
	{
		const auto it = columns.find(name);
		if (it == columns.end() || index >= it->second.second.size())
		{
			return nullptr;
		}
		return &it->second.second[index];
	}

	void PointSet::Append(const PointSet& other)
	{
		const size_t oldCount = points.size();
		const size_t addCount = other.points.size();

		// Columns only the other set has
		for (const auto& entry : other.columns)
		{
			if (columns.find(entry.first) == columns.end())
			{
				auto& created = columns[entry.first];
				created.first = entry.second.first;
				created.second.assign(oldCount, DefaultValue(entry.second.first));
			}
		}

		for (auto& entry : columns)
		{
			auto& column = entry.second;
			column.second.resize(oldCount, DefaultValue(column.first));

			const auto otherIt = other.columns.find(entry.first);
			const bool compatible = otherIt != other.columns.end() && otherIt->second.first == column.first;
			for (size_t i = 0; i < addCount; ++i)
			{
				if (compatible && i < otherIt->second.second.size())
				{
					column.second.push_back(otherIt->second.second[i]);
				}
				else
				{
					column.second.push_back(DefaultValue(column.first));
				}
			}
		}

		points.insert(points.end(), other.points.begin(), other.points.end());
	}

	void PointSet::Keep(const std::vector<uint8>& keepMask)
	{
		const size_t count = points.size();
		std::vector<Point> keptPoints;
		keptPoints.reserve(count);
		std::vector<size_t> keptIndices;
		keptIndices.reserve(count);

		for (size_t i = 0; i < count; ++i)
		{
			if (i < keepMask.size() && keepMask[i] != 0)
			{
				keptPoints.push_back(points[i]);
				keptIndices.push_back(i);
			}
		}

		for (auto& entry : columns)
		{
			auto& column = entry.second;
			std::vector<AttributeValue> kept;
			kept.reserve(keptIndices.size());
			for (const size_t i : keptIndices)
			{
				kept.push_back(i < column.second.size() ? column.second[i] : DefaultValue(column.first));
			}
			column.second = std::move(kept);
		}

		points = std::move(keptPoints);
	}

	namespace
	{
		/// FNV-1a style accumulator over raw bytes; platform independent for the types fed in.
		class Hasher
		{
		public:
			void Bytes(const void* data, const size_t size)
			{
				const uint8* p = static_cast<const uint8*>(data);
				for (size_t i = 0; i < size; ++i)
				{
					m_state ^= p[i];
					m_state *= 1099511628211ull;
				}
			}

			void U64(const uint64 v)
			{
				for (int i = 0; i < 8; ++i)
				{
					const uint8 b = static_cast<uint8>((v >> (i * 8)) & 0xFF);
					Bytes(&b, 1);
				}
			}

			void F32(const float v)
			{
				uint32 bits = 0;
				std::memcpy(&bits, &v, sizeof(bits));
				U64(bits);
			}

			void Vec(const Vector3& v)
			{
				F32(v.x);
				F32(v.y);
				F32(v.z);
			}

			void Str(const String& s)
			{
				U64(s.size());
				Bytes(s.data(), s.size());
			}

			void Value(const AttributeValue& v)
			{
				U64(v.index());
				switch (TypeOf(v))
				{
				case AttributeType::Float: F32(std::get<float>(v)); break;
				case AttributeType::Int: U64(static_cast<uint64>(std::get<int64>(v))); break;
				case AttributeType::Bool: U64(std::get<bool>(v) ? 1 : 0); break;
				case AttributeType::Vector3: Vec(std::get<Vector3>(v)); break;
				case AttributeType::String: Str(std::get<String>(v)); break;
				}
			}

			uint64 Result() const { return m_state; }

		private:
			uint64 m_state = 14695981039346656037ull;
		};

		void HashItem(Hasher& h, const DataItem& item)
		{
			h.U64(static_cast<uint64>(item.kind));
			h.U64(item.sourceId);

			h.U64(item.tags.size());
			for (const String& tag : item.tags)
			{
				h.Str(tag);
			}

			h.U64(item.attributes.size());
			for (const auto& entry : item.attributes)
			{
				h.Str(entry.first);
				h.Value(entry.second);
			}

			h.U64(item.points.points.size());
			for (const Point& p : item.points.points)
			{
				h.Vec(p.position);
				h.F32(p.rotation.w);
				h.F32(p.rotation.x);
				h.F32(p.rotation.y);
				h.F32(p.rotation.z);
				h.Vec(p.scale);
				h.F32(p.density);
				h.U64(p.seed);
				h.Vec(p.boundsMin);
				h.Vec(p.boundsMax);
			}

			h.U64(item.points.columns.size());
			for (const auto& entry : item.points.columns)
			{
				h.Str(entry.first);
				h.U64(static_cast<uint64>(entry.second.first));
				h.U64(entry.second.second.size());
				for (const AttributeValue& v : entry.second.second)
				{
					h.Value(v);
				}
			}

			h.U64(item.spline.closed ? 1 : 0);
			h.U64(static_cast<uint64>(item.spline.interpolation));
			h.U64(item.spline.points.size());
			for (const SplinePoint& sp : item.spline.points)
			{
				h.F32(sp.x);
				h.F32(sp.z);
				h.F32(sp.width);
				h.F32(sp.y);
				h.U64(sp.autoTangents ? 1 : 0);
				h.F32(sp.inX);
				h.F32(sp.inZ);
				h.F32(sp.outX);
				h.F32(sp.outZ);
				h.F32(sp.inY);
				h.F32(sp.outY);
			}
		}
	}

	uint64 ContentHash(const Collection& c)
	{
		Hasher h;
		h.U64(c.size());
		for (const DataItem& item : c)
		{
			HashItem(h, item);
		}
		return h.Result();
	}
}
