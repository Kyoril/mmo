// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "section_path_record.h"
#include "record_io.h"

namespace mmo::pcg
{
	using namespace record_io;

	namespace
	{
		/// PCGP as little endian characters.
		constexpr uint32 MagicPcgp = 0x50474350;
		/// PCGO as little endian characters (the oasis record's magic).
		constexpr uint32 MagicPcgo = 0x4F474350;
		/// 1: initial layout. Placements use the oasis record's version 4 layout.
		constexpr uint32 FormatVersion = 1;
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
		return true;
	}

	bool ReadSectionPathRecord(io::Reader& reader, SectionPathRecord& out)
	{
		uint32 magic = 0;
		uint32 version = 0;
		if (!Get(reader, magic) || magic != MagicPcgp || !Get(reader, version) || version != FormatVersion)
		{
			return false;
		}

		out = SectionPathRecord();
		uint8 sequential = 0;
		uint8 heightMode = 0;
		if (!Get(reader, out.id) || !GetString(reader, out.name) || !GetString(reader, out.sectionAsset) ||
			!Get(reader, out.params.seed) || !Get(reader, sequential) || sequential > 1 ||
			!Get(reader, out.params.joinBlend) || !Get(reader, out.params.propDensity) || !Get(reader, heightMode) ||
			heightMode > static_cast<uint8>(SectionHeightMode::FollowGround))
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

		return GetOwnedContent(reader, out.content, PlacementVersion);
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
		default:
			return RecordKind::Unknown;
		}
	}
}
