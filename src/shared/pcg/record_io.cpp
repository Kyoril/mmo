// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "record_io.h"

#include <type_traits>

namespace mmo::pcg::record_io
{
	namespace
	{
		constexpr uint32 MaxStringLength = 1u << 20;

		template<typename T>
		void PutValue(io::Writer& w, const T& v)
		{
			Put(w, v);
		}

		void PutValue(io::Writer& w, const String& v)
		{
			PutString(w, v);
		}

		template<typename T>
		bool GetValue(io::Reader& r, T& v)
		{
			return Get(r, v);
		}

		bool GetValue(io::Reader& r, String& v)
		{
			return GetString(r, v);
		}

		template<typename T>
		void PutEntries(io::Writer& w, const std::vector<TerrainContribution::Entry<T>>& entries)
		{
			Put<uint32>(w, static_cast<uint32>(entries.size()));
			for (const auto& e : entries)
			{
				Put(w, e.x);
				Put(w, e.z);
				PutValue(w, e.base);
				PutValue(w, e.written);
			}
		}

		template<typename T>
		bool GetEntries(io::Reader& r, std::vector<TerrainContribution::Entry<T>>& entries)
		{
			uint32 count = 0;
			// x, z plus two values (strings: two length prefixes) at minimum.
			const size_t valueBytes = std::is_same<T, String>::value ? 4 : sizeof(T);
			if (!GetCount(r, count, 8 + 2 * valueBytes))
			{
				return false;
			}
			entries.clear();
			entries.reserve(count);
			for (uint32 i = 0; i < count; ++i)
			{
				TerrainContribution::Entry<T> e{};
				if (!Get(r, e.x) || !Get(r, e.z) || !GetValue(r, e.base) || !GetValue(r, e.written))
				{
					return false;
				}
				entries.push_back(std::move(e));
			}
			return true;
		}

		void PutPlacement(io::Writer& w, const Placement& p)
		{
			Put(w, p.key);
			Put<uint8>(w, static_cast<uint8>(p.kind));
			PutString(w, p.asset);
			Put(w, p.position.x);
			Put(w, p.position.y);
			Put(w, p.position.z);
			Put(w, p.rotation.w);
			Put(w, p.rotation.x);
			Put(w, p.rotation.y);
			Put(w, p.rotation.z);
			Put(w, p.scale.x);
			Put(w, p.scale.y);
			Put(w, p.scale.z);
			Put<uint8>(w, p.collides ? 1 : 0);

			Put<uint32>(w, static_cast<uint32>(p.materialOverrides.size()));
			for (const PlacementMaterialOverride& o : p.materialOverrides)
			{
				Put(w, o.materialIndex);
				PutString(w, o.materialName);
			}
			Put<uint32>(w, static_cast<uint32>(p.kitPaths.size()));
			for (const String& kit : p.kitPaths)
			{
				PutString(w, kit);
			}
		}

		bool GetPlacement(io::Reader& r, Placement& p, const uint32 version)
		{
			uint8 kind = 0;
			uint8 collides = 0;
			if (!Get(r, p.key) || !Get(r, kind) || kind > static_cast<uint8>(PlacementKind::WorldModel) || !GetString(r, p.asset))
			{
				return false;
			}
			p.kind = static_cast<PlacementKind>(kind);
			if (!Get(r, p.position.x) || !Get(r, p.position.y) || !Get(r, p.position.z) ||
				!Get(r, p.rotation.w) || !Get(r, p.rotation.x) || !Get(r, p.rotation.y) || !Get(r, p.rotation.z) ||
				!Get(r, p.scale.x) || !Get(r, p.scale.y) || !Get(r, p.scale.z) || !Get(r, collides))
			{
				return false;
			}
			p.collides = collides != 0;
			if (version < 4)
			{
				return true;
			}

			uint32 overrideCount = 0;
			// Index byte plus string length prefix at minimum.
			if (!GetCount(r, overrideCount, 5))
			{
				return false;
			}
			p.materialOverrides.resize(overrideCount);
			for (PlacementMaterialOverride& o : p.materialOverrides)
			{
				if (!Get(r, o.materialIndex) || !GetString(r, o.materialName))
				{
					return false;
				}
			}

			uint32 kitCount = 0;
			if (!GetCount(r, kitCount, 4))
			{
				return false;
			}
			p.kitPaths.resize(kitCount);
			for (String& kit : p.kitPaths)
			{
				if (!GetString(r, kit))
				{
					return false;
				}
			}
			return true;
		}
	}

	void PutString(io::Writer& w, const String& s)
	{
		Put<uint32>(w, static_cast<uint32>(s.size()));
		if (!s.empty())
		{
			w.Sink().Write(s.data(), s.size());
		}
	}

	size_t Remaining(io::Reader& r)
	{
		const io::ISource* source = r.getSource();
		if (source == nullptr)
		{
			return 0;
		}
		return source->size() - source->position();
	}

	bool GetString(io::Reader& r, String& s)
	{
		uint32 length = 0;
		if (!Get(r, length) || length > MaxStringLength || length > Remaining(r))
		{
			return false;
		}
		s.assign(length, '\0');
		if (length > 0 && r.getSource()->read(&s[0], length) != length)
		{
			return false;
		}
		return true;
	}

	bool GetCount(io::Reader& r, uint32& count, size_t minElementBytes)
	{
		if (!Get(r, count))
		{
			return false;
		}
		return static_cast<uint64>(count) * minElementBytes <= Remaining(r);
	}

	void PutRect(io::Writer& w, const WorldRect& r)
	{
		Put(w, r.minX);
		Put(w, r.minZ);
		Put(w, r.maxX);
		Put(w, r.maxZ);
	}

	bool GetRect(io::Reader& r, WorldRect& v)
	{
		return Get(r, v.minX) && Get(r, v.minZ) && Get(r, v.maxX) && Get(r, v.maxZ);
	}

	void PutSplinePoint(io::Writer& w, const SplinePoint& pt)
	{
		Put(w, pt.x);
		Put(w, pt.z);
		Put(w, pt.width);
		Put<uint8>(w, pt.autoTangents ? 1 : 0);
		Put(w, pt.inX);
		Put(w, pt.inZ);
		Put(w, pt.outX);
		Put(w, pt.outZ);
		Put(w, pt.y);
		Put(w, pt.inY);
		Put(w, pt.outY);
	}

	bool GetSplinePoint(io::Reader& r, SplinePoint& pt)
	{
		uint8 autoTangents = 1;
		if (!Get(r, pt.x) || !Get(r, pt.z) || !Get(r, pt.width) || !Get(r, autoTangents) || autoTangents > 1 ||
			!Get(r, pt.inX) || !Get(r, pt.inZ) || !Get(r, pt.outX) || !Get(r, pt.outZ) ||
			!Get(r, pt.y) || !Get(r, pt.inY) || !Get(r, pt.outY))
		{
			return false;
		}
		pt.autoTangents = autoTangents != 0;
		return true;
	}

	void PutOwnedContent(io::Writer& writer, const OwnedContent& content)
	{
		PutRect(writer, content.influence);

		const TerrainContribution& t = content.terrain;
		PutEntries(writer, t.outer);
		PutEntries(writer, t.inner);
		PutEntries(writer, t.waterHeights);
		PutEntries(writer, t.splat);
		PutEntries(writer, t.waterQuads);
		PutEntries(writer, t.materials);
		Put(writer, t.touchedOuter.minX);
		Put(writer, t.touchedOuter.minZ);
		Put(writer, t.touchedOuter.maxX);
		Put(writer, t.touchedOuter.maxZ);

		Put<uint32>(writer, static_cast<uint32>(content.placements.size()));
		for (const OwnedPlacement& op : content.placements)
		{
			Put(writer, op.key);
			Put(writer, op.uniqueId);
			Put<uint8>(writer, static_cast<uint8>(op.kind));
			PutPlacement(writer, op.written);
		}
	}

	bool GetOwnedContent(io::Reader& reader, OwnedContent& content, const uint32 version)
	{
		if (!GetRect(reader, content.influence))
		{
			return false;
		}

		TerrainContribution& t = content.terrain;
		if (!GetEntries(reader, t.outer) || !GetEntries(reader, t.inner) || !GetEntries(reader, t.waterHeights) ||
			!GetEntries(reader, t.splat) || !GetEntries(reader, t.waterQuads) || !GetEntries(reader, t.materials) ||
			!Get(reader, t.touchedOuter.minX) || !Get(reader, t.touchedOuter.minZ) ||
			!Get(reader, t.touchedOuter.maxX) || !Get(reader, t.touchedOuter.maxZ))
		{
			return false;
		}

		uint32 placementCount = 0;
		if (!GetCount(reader, placementCount, 17))
		{
			return false;
		}
		content.placements.reserve(placementCount);
		for (uint32 i = 0; i < placementCount; ++i)
		{
			OwnedPlacement op;
			uint8 kind = 0;
			if (!Get(reader, op.key) || !Get(reader, op.uniqueId) || !Get(reader, kind) ||
				kind > static_cast<uint8>(PlacementKind::WorldModel) || !GetPlacement(reader, op.written, version))
			{
				return false;
			}
			op.kind = static_cast<PlacementKind>(kind);
			content.placements.push_back(std::move(op));
		}
		return true;
	}
}
