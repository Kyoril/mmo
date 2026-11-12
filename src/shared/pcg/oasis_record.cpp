// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "oasis_record.h"
#include "record_io.h"

#include "binary_io/memory_source.h"
#include "binary_io/vector_sink.h"

#include <type_traits>

namespace mmo::pcg
{
	using namespace record_io;

	namespace
	{
		/// PCGO as little endian characters.
		constexpr uint32 MagicPcgo = 0x4F474350;
		/// 1: loop points x, z, width. 2: adds per-point auto flag and in/out tangent handles (x/z).
		/// 3: adds per-point height and tangent heights.
		/// 4: placements add material overrides and prop kit paths (and the WorldModel kind).
		constexpr uint32 FormatVersion = 4;
		constexpr uint32 MinFormatVersion = 1;

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
			PutSplinePoint(writer, pt);
		}

		PutOwnedContent(writer, record.content);

		return true;
	}

	bool ReadOasisRecord(io::Reader& reader, OasisRecord& out)
	{
		uint32 magic = 0;
		uint32 version = 0;
		if (!Get(reader, magic) || magic != MagicPcgo || !Get(reader, version) ||
			version < MinFormatVersion || version > FormatVersion)
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
		const bool hasTangents = version >= 2;
		const bool hasHeights = version >= 3;
		const size_t pointBytes = hasHeights ? 41 : (hasTangents ? 29 : 12);
		if (!Get(reader, closed) || !Get(reader, interp) ||
			interp > static_cast<uint8>(SplineInterpolation::CatmullRom) || !GetCount(reader, pointCount, pointBytes))
		{
			return false;
		}
		out.heightsValid = hasHeights;
		out.loop.closed = closed != 0;
		out.loop.interpolation = static_cast<SplineInterpolation>(interp);
		out.loop.points.resize(pointCount);
		for (SplinePoint& pt : out.loop.points)
		{
			if (!Get(reader, pt.x) || !Get(reader, pt.z) || !Get(reader, pt.width))
			{
				return false;
			}

			// Version 1 points keep the defaults: automatic tangents.
			if (hasTangents)
			{
				uint8 autoTangents = 1;
				if (!Get(reader, autoTangents) || autoTangents > 1 ||
					!Get(reader, pt.inX) || !Get(reader, pt.inZ) || !Get(reader, pt.outX) || !Get(reader, pt.outZ))
				{
					return false;
				}
				pt.autoTangents = autoTangents != 0;
			}

			// Older points have no height (zero); heightsValid tells the editor to fill it.
			if (hasHeights && (!Get(reader, pt.y) || !Get(reader, pt.inY) || !Get(reader, pt.outY)))
			{
				return false;
			}
		}

		return GetOwnedContent(reader, out.content, version);
	}
}
