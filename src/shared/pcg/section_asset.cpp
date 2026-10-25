// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "section_asset.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace mmo::pcg
{
	namespace
	{
		/// PSEC as little endian characters.
		constexpr uint32 MagicPsec = 0x43455350;
		constexpr uint32 FormatVersion = 3;
		/// Oldest version the reader accepts (version 1 has no runs).
		constexpr uint32 MinFormatVersion = 1;
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

		void PutStrings(io::Writer& w, const std::vector<String>& list)
		{
			Put<uint32>(w, static_cast<uint32>(list.size()));
			for (const String& s : list)
			{
				PutString(w, s);
			}
		}

		bool GetStrings(io::Reader& r, std::vector<String>& list)
		{
			uint32 count = 0;
			if (!GetCount(r, count, 4))
			{
				return false;
			}
			list.assign(count, String());
			for (String& s : list)
			{
				if (!GetString(r, s))
				{
					return false;
				}
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
			PutStrings(w, p.kitPaths);
		}

		bool GetPlacement(io::Reader& r, Placement& p)
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
			return GetStrings(r, p.kitPaths);
		}

		uint64 CellCount(const uint32 width, const uint32 depth)
		{
			return static_cast<uint64>(width) * depth;
		}

		/// Bilinear sample setup shared by height and paint: the four cell indices and weights.
		struct Cell
		{
			uint32 x0, z0, x1, z1;
			float tx, tz;
		};

		Cell LocateCell(const float spacing, const int32 originX, const int32 originZ,
			const uint32 width, const uint32 depth, const float localX, const float localZ)
		{
			const float inv = spacing > 0.0f ? 1.0f / spacing : 0.0f;
			const float fx = std::max(0.0f, std::min(static_cast<float>(width - 1), localX * inv - static_cast<float>(originX)));
			const float fz = std::max(0.0f, std::min(static_cast<float>(depth - 1), localZ * inv - static_cast<float>(originZ)));

			Cell c;
			c.x0 = static_cast<uint32>(std::floor(fx));
			c.z0 = static_cast<uint32>(std::floor(fz));
			c.x1 = std::min(c.x0 + 1, width - 1);
			c.z1 = std::min(c.z0 + 1, depth - 1);
			c.tx = fx - static_cast<float>(c.x0);
			c.tz = fz - static_cast<float>(c.z0);
			return c;
		}

		float Lerp(const float a, const float b, const float t)
		{
			return a + (b - a) * t;
		}

		void PutGridHeader(io::Writer& w, const float spacing, const int32 originX, const int32 originZ,
			const uint32 width, const uint32 depth)
		{
			Put(w, spacing);
			Put(w, originX);
			Put(w, originZ);
			Put(w, width);
			Put(w, depth);
		}

		bool GetGridHeader(io::Reader& r, float& spacing, int32& originX, int32& originZ, uint32& width, uint32& depth)
		{
			return Get(r, spacing) && Get(r, originX) && Get(r, originZ) && Get(r, width) && Get(r, depth);
		}

		void PutRuns(io::Writer& w, const std::vector<SectionRun>& runs)
		{
			Put<uint32>(w, static_cast<uint32>(runs.size()));
			for (const SectionRun& run : runs)
			{
				PutString(w, run.name);
				Put(w, run.keepChance);
				Put(w, run.x);
				Put(w, run.startZ);
				Put(w, run.endZ);
				Put<uint32>(w, static_cast<uint32>(run.pieces.size()));
				for (const SectionRunPiece& piece : run.pieces)
				{
					PutPlacement(w, piece.placement);
					Put(w, piece.heightAboveTerrain);
					Put(w, piece.length);
					Put(w, piece.startOffset);
				}
			}
		}

		bool GetRuns(io::Reader& r, std::vector<SectionRun>& runs)
		{
			uint32 runCount = 0;
			// Name length prefix, four floats and the piece count.
			if (!GetCount(r, runCount, 24))
			{
				return false;
			}
			runs.resize(runCount);
			for (SectionRun& run : runs)
			{
				uint32 pieceCount = 0;
				if (!GetString(r, run.name) || !Get(r, run.keepChance) || !Get(r, run.x) || !Get(r, run.startZ) || !Get(r, run.endZ) ||
					!GetCount(r, pieceCount, 60))
				{
					return false;
				}
				run.pieces.resize(pieceCount);
				for (SectionRunPiece& piece : run.pieces)
				{
					if (!GetPlacement(r, piece.placement) || !Get(r, piece.heightAboveTerrain) || !Get(r, piece.length) || !Get(r, piece.startOffset))
					{
						return false;
					}
				}
			}
			return true;
		}
	}

	bool WriteSectionAsset(io::Writer& writer, const SectionAsset& asset)
	{
		// Validate before writing a single byte.
		for (const SectionVariant& v : asset.variants)
		{
			if (v.heights.values.size() != CellCount(v.heights.width, v.heights.depth) ||
				v.paint.packed.size() != CellCount(v.paint.width, v.paint.depth) ||
				v.paint.coverage.size() != v.paint.packed.size())
			{
				return false;
			}
		}

		Put(writer, MagicPsec);
		Put(writer, FormatVersion);
		PutString(writer, asset.name);
		Put(writer, asset.length);
		Put(writer, asset.halfWidth);
		Put(writer, asset.sideFalloff);
		for (const String& texture : asset.layerTextures)
		{
			PutString(writer, texture);
		}
		Put(writer, asset.sourceStamp);

		Put<uint32>(writer, static_cast<uint32>(asset.rules.size()));
		for (const PropRule& rule : asset.rules)
		{
			PutString(writer, rule.asset);
			Put(writer, rule.keepChance);
			Put(writer, rule.positionJitter);
			Put(writer, rule.yawJitterDeg);
			Put(writer, rule.scaleJitter);
			PutStrings(writer, rule.swapGroup);
		}

		Put<uint32>(writer, static_cast<uint32>(asset.variants.size()));
		for (const SectionVariant& v : asset.variants)
		{
			Put(writer, v.originX);

			PutGridHeader(writer, v.heights.spacing, v.heights.originX, v.heights.originZ, v.heights.width, v.heights.depth);
			for (const float h : v.heights.values)
			{
				Put(writer, h);
			}

			PutGridHeader(writer, v.paint.spacing, v.paint.originX, v.paint.originZ, v.paint.width, v.paint.depth);
			for (const uint32 p : v.paint.packed)
			{
				Put(writer, p);
			}
			for (const uint8 c : v.paint.coverage)
			{
				Put(writer, c);
			}

			Put<uint32>(writer, static_cast<uint32>(v.placements.size()));
			for (const SectionPlacement& sp : v.placements)
			{
				PutPlacement(writer, sp.placement);
				Put(writer, sp.heightAboveTerrain);
			}

			PutRuns(writer, v.runs);
		}

		// Version 3 trailer: corner angle, then the role of every variant.
		Put(writer, asset.cornerAngleDeg);
		for (const SectionVariant& v : asset.variants)
		{
			Put<uint8>(writer, static_cast<uint8>(v.role));
		}
		return true;
	}

	bool ReadSectionAsset(io::Reader& reader, SectionAsset& out)
	{
		uint32 magic = 0;
		uint32 version = 0;
		if (!Get(reader, magic) || magic != MagicPsec || !Get(reader, version) || version < MinFormatVersion || version > FormatVersion)
		{
			return false;
		}

		out = SectionAsset();
		if (!GetString(reader, out.name) || !Get(reader, out.length) || !Get(reader, out.halfWidth) ||
			!Get(reader, out.sideFalloff))
		{
			return false;
		}
		for (String& texture : out.layerTextures)
		{
			if (!GetString(reader, texture))
			{
				return false;
			}
		}
		if (!Get(reader, out.sourceStamp))
		{
			return false;
		}

		uint32 ruleCount = 0;
		// Asset length prefix, four floats and the swap group count.
		if (!GetCount(reader, ruleCount, 24))
		{
			return false;
		}
		out.rules.resize(ruleCount);
		for (PropRule& rule : out.rules)
		{
			if (!GetString(reader, rule.asset) || !Get(reader, rule.keepChance) || !Get(reader, rule.positionJitter) ||
				!Get(reader, rule.yawJitterDeg) || !Get(reader, rule.scaleJitter) || !GetStrings(reader, rule.swapGroup))
			{
				return false;
			}
		}

		uint32 variantCount = 0;
		// Origin, two grid headers and the placement count.
		if (!GetCount(reader, variantCount, 4 + 20 + 20 + 4))
		{
			return false;
		}
		out.variants.resize(variantCount);
		for (SectionVariant& v : out.variants)
		{
			if (!Get(reader, v.originX))
			{
				return false;
			}

			if (!GetGridHeader(reader, v.heights.spacing, v.heights.originX, v.heights.originZ, v.heights.width, v.heights.depth))
			{
				return false;
			}
			const uint64 heightCells = CellCount(v.heights.width, v.heights.depth);
			if (heightCells * 4 > Remaining(reader))
			{
				return false;
			}
			v.heights.values.resize(static_cast<size_t>(heightCells));
			for (float& h : v.heights.values)
			{
				if (!Get(reader, h))
				{
					return false;
				}
			}

			if (!GetGridHeader(reader, v.paint.spacing, v.paint.originX, v.paint.originZ, v.paint.width, v.paint.depth))
			{
				return false;
			}
			const uint64 paintCells = CellCount(v.paint.width, v.paint.depth);
			if (paintCells * 5 > Remaining(reader))
			{
				return false;
			}
			v.paint.packed.resize(static_cast<size_t>(paintCells));
			v.paint.coverage.resize(static_cast<size_t>(paintCells));
			for (uint32& p : v.paint.packed)
			{
				if (!Get(reader, p))
				{
					return false;
				}
			}
			for (uint8& c : v.paint.coverage)
			{
				if (!Get(reader, c))
				{
					return false;
				}
			}

			uint32 placementCount = 0;
			// A placement is well over 60 bytes; this is a conservative lower bound.
			if (!GetCount(reader, placementCount, 60))
			{
				return false;
			}
			v.placements.resize(placementCount);
			for (SectionPlacement& sp : v.placements)
			{
				if (!GetPlacement(reader, sp.placement) || !Get(reader, sp.heightAboveTerrain))
				{
					return false;
				}
			}

			if (version >= 2 && !GetRuns(reader, v.runs))
			{
				return false;
			}
		}

		if (version >= 3)
		{
			// A non-finite angle is corrupt data; a finite one is clamped to the range a turn angle can have.
			if (!Get(reader, out.cornerAngleDeg) || !std::isfinite(out.cornerAngleDeg))
			{
				return false;
			}
			out.cornerAngleDeg = std::clamp(out.cornerAngleDeg, 0.0f, 180.0f);
			for (SectionVariant& v : out.variants)
			{
				uint8 role = 0;
				if (!Get(reader, role) || role > static_cast<uint8>(SectionVariantRole::OuterCorner))
				{
					return false;
				}
				v.role = static_cast<SectionVariantRole>(role);
			}
		}
		return true;
	}

	float SampleHeight(const SectionGrid& grid, const float localX, const float localZ)
	{
		if (grid.width == 0 || grid.depth == 0 || grid.values.size() < CellCount(grid.width, grid.depth))
		{
			return 0.0f;
		}

		const Cell c = LocateCell(grid.spacing, grid.originX, grid.originZ, grid.width, grid.depth, localX, localZ);
		const auto at = [&](const uint32 x, const uint32 z) { return grid.values[static_cast<size_t>(z) * grid.width + x]; };
		const float top = Lerp(at(c.x0, c.z0), at(c.x1, c.z0), c.tx);
		const float bottom = Lerp(at(c.x0, c.z1), at(c.x1, c.z1), c.tx);
		return Lerp(top, bottom, c.tz);
	}

	void SamplePaint(const SectionPaint& paint, const float localX, const float localZ, std::array<float, 4>& weights, float& coverage)
	{
		weights = { 0.0f, 0.0f, 0.0f, 0.0f };
		coverage = 0.0f;
		const uint64 cells = CellCount(paint.width, paint.depth);
		if (cells == 0 || paint.packed.size() < cells || paint.coverage.size() < cells)
		{
			return;
		}

		const Cell c = LocateCell(paint.spacing, paint.originX, paint.originZ, paint.width, paint.depth, localX, localZ);
		const size_t i00 = static_cast<size_t>(c.z0) * paint.width + c.x0;
		const size_t i10 = static_cast<size_t>(c.z0) * paint.width + c.x1;
		const size_t i01 = static_cast<size_t>(c.z1) * paint.width + c.x0;
		const size_t i11 = static_cast<size_t>(c.z1) * paint.width + c.x1;

		for (uint32 layer = 0; layer < 4; ++layer)
		{
			const uint32 shift = layer * 8;
			const auto byteAt = [&](const size_t i) { return static_cast<float>((paint.packed[i] >> shift) & 0xFFu); };
			const float top = Lerp(byteAt(i00), byteAt(i10), c.tx);
			const float bottom = Lerp(byteAt(i01), byteAt(i11), c.tx);
			weights[layer] = Lerp(top, bottom, c.tz);
		}

		const auto cov = [&](const size_t i) { return static_cast<float>(paint.coverage[i]); };
		coverage = Lerp(Lerp(cov(i00), cov(i10), c.tx), Lerp(cov(i01), cov(i11), c.tx), c.tz);
	}

	String NormalizeTextureKey(const String& key)
	{
		String result = key;
		for (char& c : result)
		{
			if (c == '\\')
			{
				c = '/';
			}
			else
			{
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
		}
		return result;
	}

	bool TextureKeysMatch(const String& a, const String& b)
	{
		return a.size() == b.size() && NormalizeTextureKey(a) == NormalizeTextureKey(b);
	}

	bool IsContinuousRun(const SectionRun& run, const float sectionLength)
	{
		return run.startZ <= kRunContinuityTolerance && run.endZ >= sectionLength - kRunContinuityTolerance;
	}
}
