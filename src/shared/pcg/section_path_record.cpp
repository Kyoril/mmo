// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "section_path_record.h"
#include "record_io.h"

#include <cmath>

namespace mmo::pcg
{
	using namespace record_io;

	namespace
	{
		/// PCGP as little endian characters.
		constexpr uint32 MagicPcgp = 0x50474350;
		/// PCGG as little endian characters (the graph instance record's magic).
		constexpr uint32 MagicPcgg = 0x47474350;
		/// PCGO as little endian characters (the oasis record's magic).
		constexpr uint32 MagicPcgo = 0x4F474350;
		/// 1: initial layout. Placements use the oasis record's version 4 layout.
		/// 2: appends the per-copy rects of the last build (builtRects).
		/// 3: appends spline.closed, params.floorInside and params.groundSmoothing; height mode SmoothedGround.
		/// 4: appends the mass layer (massEnabled, massHeight, massDepth, massRise, massNoiseAmplitude,
		///    massNoiseWavelength, massDetailAmplitude, massDetailWavelength, massPaintLayer) and the facade variety
		///    (mirrorCopies, facadeHeightJitter).
		constexpr uint32 FormatVersion = 4;
		/// Bytes per rect written by PutRect.
		constexpr size_t RectBytes = 16;
		/// Placement layout version shared with the oasis record.
		constexpr uint32 PlacementVersion = 4;
	}

	bool WriteSectionPathRecord(io::Writer& writer, const SectionPathRecord& record)
	{
		if (record.pointIds.size() != record.spline.points.size())
		{
			return false;
		}

		Put(writer, MagicPcgp);
		Put(writer, FormatVersion);

		Put(writer, record.id);
		PutString(writer, record.name);
		PutString(writer, record.sectionAsset);

		Put(writer, record.params.seed);
		Put<uint8>(writer, record.params.sequentialVariants ? 1 : 0);
		Put(writer, record.params.joinBlend);
		Put(writer, record.params.propDensity);
		Put<uint8>(writer, static_cast<uint8>(record.params.heightMode));

		Put<uint32>(writer, static_cast<uint32>(record.spline.points.size()));
		for (size_t i = 0; i < record.spline.points.size(); ++i)
		{
			PutSplinePoint(writer, record.spline.points[i]);
			Put(writer, record.pointIds[i]);
		}
		Put(writer, record.nextPointId);

		PutOwnedContent(writer, record.content);

		Put<uint32>(writer, static_cast<uint32>(record.builtRects.size()));
		for (const WorldRect& rect : record.builtRects)
		{
			PutRect(writer, rect);
		}

		Put<uint8>(writer, record.spline.closed ? 1 : 0);
		Put<uint8>(writer, record.params.floorInside ? 1 : 0);
		Put(writer, record.params.groundSmoothing);

		const SectionPathParams& p = record.params;
		Put<uint8>(writer, p.massEnabled ? 1 : 0);
		Put(writer, p.massHeight);
		Put(writer, p.massDepth);
		Put(writer, p.massRise);
		Put(writer, p.massNoiseAmplitude);
		Put(writer, p.massNoiseWavelength);
		Put(writer, p.massDetailAmplitude);
		Put(writer, p.massDetailWavelength);
		Put<int8>(writer, p.massPaintLayer);
		Put<uint8>(writer, p.mirrorCopies ? 1 : 0);
		Put(writer, p.facadeHeightJitter);
		return true;
	}

	bool ReadSectionPathRecord(io::Reader& reader, SectionPathRecord& out)
	{
		uint32 magic = 0;
		uint32 version = 0;
		if (!Get(reader, magic) || magic != MagicPcgp || !Get(reader, version) || version < 1 || version > FormatVersion)
		{
			return false;
		}

		out = SectionPathRecord();
		uint8 sequential = 0;
		uint8 heightMode = 0;
		const SectionHeightMode lastHeightMode = version >= 3 ? SectionHeightMode::SmoothedGround : SectionHeightMode::FollowGround;
		if (!Get(reader, out.id) || !GetString(reader, out.name) || !GetString(reader, out.sectionAsset) ||
			!Get(reader, out.params.seed) || !Get(reader, sequential) || sequential > 1 ||
			!Get(reader, out.params.joinBlend) || !Get(reader, out.params.propDensity) || !Get(reader, heightMode) ||
			heightMode > static_cast<uint8>(lastHeightMode))
		{
			return false;
		}
		out.params.sequentialVariants = sequential != 0;
		out.params.heightMode = static_cast<SectionHeightMode>(heightMode);

		out.spline.closed = false;
		out.spline.interpolation = SplineInterpolation::CatmullRom;
		uint32 pointCount = 0;
		if (!GetCount(reader, pointCount, SplinePointBytes + 8))
		{
			return false;
		}
		out.spline.points.resize(pointCount);
		out.pointIds.resize(pointCount);
		for (uint32 i = 0; i < pointCount; ++i)
		{
			if (!GetSplinePoint(reader, out.spline.points[i]) || !Get(reader, out.pointIds[i]))
			{
				return false;
			}
		}
		if (!Get(reader, out.nextPointId))
		{
			return false;
		}

		// A damaged or hand-edited file could hand out an id that is already in use: never go below max + 1.
		for (const uint64 id : out.pointIds)
		{
			if (id >= out.nextPointId)
			{
				out.nextPointId = id + 1;
			}
		}

		if (!GetOwnedContent(reader, out.content, PlacementVersion))
		{
			return false;
		}

		if (version < 2)
		{
			// Version 1 has no per-copy rects; the overlap check falls back to the influence box.
			return true;
		}

		uint32 rectCount = 0;
		if (!GetCount(reader, rectCount, RectBytes))
		{
			return false;
		}
		out.builtRects.resize(rectCount);
		for (WorldRect& rect : out.builtRects)
		{
			if (!GetRect(reader, rect))
			{
				return false;
			}
		}

		if (version < 3)
		{
			// Version 2 paths are open, with the default loop settings.
			return true;
		}

		uint8 closed = 0;
		uint8 floorInside = 0;
		if (!Get(reader, closed) || closed > 1 || !Get(reader, floorInside) || floorInside > 1 ||
			!Get(reader, out.params.groundSmoothing))
		{
			return false;
		}
		if (!std::isfinite(out.params.groundSmoothing))
		{
			return false;
		}
		if (out.params.groundSmoothing < 0.0f)
		{
			// A negative window means no smoothing (the layout treats w <= 0 as FollowGround).
			out.params.groundSmoothing = 0.0f;
		}
		out.spline.closed = closed != 0;
		out.params.floorInside = floorInside != 0;

		if (version < 4)
		{
			// Version 3 has no mass layer and no facade variety (both off).
			return true;
		}

		SectionPathParams& p = out.params;
		uint8 massEnabled = 0;
		uint8 mirrorCopies = 0;
		if (!Get(reader, massEnabled) || massEnabled > 1 || !Get(reader, p.massHeight) || !Get(reader, p.massDepth) ||
			!Get(reader, p.massRise) || !Get(reader, p.massNoiseAmplitude) || !Get(reader, p.massNoiseWavelength) ||
			!Get(reader, p.massDetailAmplitude) || !Get(reader, p.massDetailWavelength) || !Get(reader, p.massPaintLayer) ||
			p.massPaintLayer < -1 || p.massPaintLayer > 3 || !Get(reader, mirrorCopies) || mirrorCopies > 1 ||
			!Get(reader, p.facadeHeightJitter))
		{
			return false;
		}
		for (const float value : { p.massHeight, p.massDepth, p.massRise, p.massNoiseAmplitude, p.massNoiseWavelength,
			p.massDetailAmplitude, p.massDetailWavelength, p.facadeHeightJitter })
		{
			if (!std::isfinite(value))
			{
				return false;
			}
		}
		p.massEnabled = massEnabled != 0;
		p.mirrorCopies = mirrorCopies != 0;
		return true;
	}

	RecordKind PeekRecordKind(io::Reader& reader)
	{
		io::ISource* source = reader.getSource();
		if (source == nullptr)
		{
			return RecordKind::Unknown;
		}

		const size_t start = source->position();
		uint32 magic = 0;
		const bool ok = Get(reader, magic);
		source->seek(start);
		if (!ok)
		{
			return RecordKind::Unknown;
		}

		switch (magic)
		{
		case MagicPcgo:
			return RecordKind::Oasis;
		case MagicPcgp:
			return RecordKind::SectionPath;
		case MagicPcgg:
			return RecordKind::GraphInstance;
		default:
			return RecordKind::Unknown;
		}
	}
}
