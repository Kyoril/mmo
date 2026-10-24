// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "oasis_record.h"

#include "binary_io/memory_source.h"
#include "binary_io/vector_sink.h"

#include <type_traits>

namespace mmo::pcg
{
	namespace
	{
		/// PCGO as little endian characters.
		constexpr uint32 MagicPcgo = 0x4F474350;
		constexpr uint32 FormatVersion = 1;
		constexpr uint32 MaxStringLength = 1u << 20;

		template<typename T>
		void Put(io::Writer& w, const T& value)
		{
			w.WritePOD(value);
		}

		void PutString(io::Writer& w, const String& s)
		{
			Put<uint32>(w, static_cast<uint32>(s.size()));
			if (!s.empty())
			{
				w.Sink().Write(s.data(), s.size());
			}
		}

		template<typename T>
		bool Get(io::Reader& r, T& value)
		{
			r.readPOD(value);
			return static_cast<bool>(r);
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

		/// Reads an element count and rejects counts that cannot fit in the remaining data.
		bool GetCount(io::Reader& r, uint32& count, size_t minElementBytes)
		{
			if (!Get(r, count))
			{
				return false;
			}
			return static_cast<uint64>(count) * minElementBytes <= Remaining(r);
		}

		/// Visits OasisParams fields in a fixed order (append new fields at the end only).
		template<typename Visitor>
		void VisitParams(Visitor& v, OasisParams& p)
		{
			v(p.basinDepth); v(p.rimWidth); v(p.outerFalloff); v(p.terraceSteps); v(p.terraceSharpness);
			v(p.floorNoiseAmplitude); v(p.rimPercentile); v(p.waterInset); v(p.waterDepth);
			v(p.grassBand); v(p.dirtBand); v(p.bandNoiseAmplitude);
			v(p.rockSpacing); v(p.rockOffset); v(p.rockOffsetJitter); v(p.rockYawJitterDeg);
			v(p.rockScaleMin); v(p.rockScaleMax); v(p.rockSink);
			v(p.cliffWeight); v(p.shelfWeight); v(p.clumpWeight);
			v(p.palmCell); v(p.palmDensity); v(p.palmNearWaterBoost); v(p.palmScaleMin); v(p.palmScaleMax);
			v(p.bushCell); v(p.bushDensity); v(p.waterPlantBand); v(p.waterPlantDensity); v(p.maxSlopeDeg);
			v(p.shelfSink); v(p.clumpSink);
		}

		struct ParamWriter
		{
			io::Writer& w;

			template<typename T>
			void operator()(T& value)
			{
				Put(w, value);
			}
		};

		struct ParamReader
		{
			io::Reader& r;
			size_t remaining;
			bool ok = true;

			template<typename T>
			void operator()(T& value)
			{
				// Fields missing in older blocks keep their defaults.
				if (!ok || remaining < sizeof(T))
				{
					return;
				}
				ok = Get(r, value);
				remaining -= sizeof(T);
			}
		};

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
		}

		bool GetPlacement(io::Reader& r, Placement& p)
		{
			uint8 kind = 0;
			uint8 collides = 0;
			if (!Get(r, p.key) || !Get(r, kind) || kind > static_cast<uint8>(PlacementKind::Foliage) || !GetString(r, p.asset))
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
			return true;
		}
	}

	bool WriteOasisRecord(io::Writer& writer, const OasisRecord& record)
	{
		Put(writer, MagicPcgo);
		Put(writer, FormatVersion);

		Put(writer, record.id);
		PutString(writer, record.name);
		Put(writer, record.seed);

		// Params: length-prefixed block so later versions can append fields.
		{
			std::vector<char> buffer;
			io::VectorSink<char> sink(buffer);
			io::Writer block(sink);
			OasisParams copy = record.params;
			ParamWriter pw{ block };
			VisitParams(pw, copy);
			Put<uint32>(writer, static_cast<uint32>(buffer.size()));
			writer.Sink().Write(buffer.data(), buffer.size());
		}

		Put<uint8>(writer, record.loop.closed ? 1 : 0);
		Put<uint8>(writer, static_cast<uint8>(record.loop.interpolation));
		Put<uint32>(writer, static_cast<uint32>(record.loop.points.size()));
		for (const SplinePoint& pt : record.loop.points)
		{
			Put(writer, pt.x);
			Put(writer, pt.z);
			Put(writer, pt.width);
		}

		PutRect(writer, record.influence);

		const TerrainContribution& t = record.terrain;
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

		Put<uint32>(writer, static_cast<uint32>(record.placements.size()));
		for (const OwnedPlacement& op : record.placements)
		{
			Put(writer, op.key);
			Put(writer, op.uniqueId);
			Put<uint8>(writer, static_cast<uint8>(op.kind));
			PutPlacement(writer, op.written);
		}

		return true;
	}

	bool ReadOasisRecord(io::Reader& reader, OasisRecord& out)
	{
		uint32 magic = 0;
		uint32 version = 0;
		if (!Get(reader, magic) || magic != MagicPcgo || !Get(reader, version) || version != FormatVersion)
		{
			return false;
		}

		out = OasisRecord();
		if (!Get(reader, out.id) || !GetString(reader, out.name) || !Get(reader, out.seed))
		{
			return false;
		}

		uint32 blockSize = 0;
		if (!Get(reader, blockSize) || blockSize > Remaining(reader))
		{
			return false;
		}
		{
			ParamReader pr{ reader, blockSize };
			VisitParams(pr, out.params);
			if (!pr.ok)
			{
				return false;
			}
			// Skip fields from a newer writer.
			if (pr.remaining > 0)
			{
				reader.skip(pr.remaining);
			}
		}

		uint8 closed = 0;
		uint8 interp = 0;
		uint32 pointCount = 0;
		if (!Get(reader, closed) || !Get(reader, interp) ||
			interp > static_cast<uint8>(SplineInterpolation::CatmullRom) || !GetCount(reader, pointCount, 12))
		{
			return false;
		}
		out.loop.closed = closed != 0;
		out.loop.interpolation = static_cast<SplineInterpolation>(interp);
		out.loop.points.resize(pointCount);
		for (SplinePoint& pt : out.loop.points)
		{
			if (!Get(reader, pt.x) || !Get(reader, pt.z) || !Get(reader, pt.width))
			{
				return false;
			}
		}

		if (!GetRect(reader, out.influence))
		{
			return false;
		}

		TerrainContribution& t = out.terrain;
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
		out.placements.reserve(placementCount);
		for (uint32 i = 0; i < placementCount; ++i)
		{
			OwnedPlacement op;
			uint8 kind = 0;
			if (!Get(reader, op.key) || !Get(reader, op.uniqueId) || !Get(reader, kind) ||
				kind > static_cast<uint8>(PlacementKind::Foliage) || !GetPlacement(reader, op.written))
			{
				return false;
			}
			op.kind = static_cast<PlacementKind>(kind);
			out.placements.push_back(std::move(op));
		}

		return true;
	}
}
