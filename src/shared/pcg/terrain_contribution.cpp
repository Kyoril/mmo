// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "terrain_contribution.h"

#include "parallel_for.h"

#include "base/macros.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace mmo::pcg
{
	bool RectsOverlap(const WorldRect& a, const WorldRect& b)
	{
		return a.minX <= b.maxX && b.minX <= a.maxX && a.minZ <= b.maxZ && b.minZ <= a.maxZ;
	}

	IntRect TileRangeOverlapping(const GridLayout& layout, const WorldRect& rect)
	{
		IntRect out{ 0, 0, -1, -1 };
		const double tileSize = layout.vertexSpacing * static_cast<double>(layout.verticesPerTile);
		if (!(tileSize > 0.0) || !std::isfinite(tileSize) || !(rect.minX <= rect.maxX) || !(rect.minZ <= rect.maxZ))
		{
			return out;
		}

		// First tile: the one the min edge lies in; last: the one before the tile the max edge starts, unless the rect
		// has no extent there (then the min tile).
		const auto range = [tileSize](const float min, const float max, const double origin, const uint32 tiles, int32& lo, int32& hi)
		{
			const double first = std::floor((static_cast<double>(min) - origin) / tileSize);
			const double last = std::max(first, std::ceil((static_cast<double>(max) - origin) / tileSize) - 1.0);
			double clampedLo = first;
			double clampedHi = last;
			if (tiles > 0)
			{
				clampedLo = std::max(clampedLo, 0.0);
				clampedHi = std::min(clampedHi, static_cast<double>(tiles) - 1.0);
			}
			if (!(clampedLo <= clampedHi) || std::abs(clampedLo) > 2.0e9 || std::abs(clampedHi) > 2.0e9)
			{
				lo = 0;
				hi = -1;
				return;
			}
			lo = static_cast<int32>(clampedLo);
			hi = static_cast<int32>(clampedHi);
		};
		int32 x0 = 0, x1 = -1, z0 = 0, z1 = -1;
		range(rect.minX, rect.maxX, layout.originX, layout.tilesX, x0, x1);
		range(rect.minZ, rect.maxZ, layout.originZ, layout.tilesZ, z0, z1);
		if (x0 > x1 || z0 > z1)
		{
			return out;
		}
		out.minX = x0;
		out.maxX = x1;
		out.minZ = z0;
		out.maxZ = z1;
		return out;
	}

	bool TerrainContribution::IsEmpty() const
	{
		return outer.empty() && inner.empty() && waterHeights.empty() && splat.empty() && waterQuads.empty() && materials.empty();
	}

	void TerrainContribution::Clear()
	{
		outer.clear();
		inner.clear();
		waterHeights.clear();
		splat.clear();
		waterQuads.clear();
		materials.clear();
		touchedOuter = IntRect{};
	}

	namespace
	{
		struct IndexRange
		{
			int32 lo, hi; // inclusive, empty if hi < lo
		};

		/// Indices i with minW <= origin + (i + offset) * step <= maxW.
		IndexRange RangeFor(double minW, double maxW, double origin, double step, double offset)
		{
			IndexRange r;
			r.lo = static_cast<int32>(std::ceil((minW - origin) / step - offset));
			r.hi = static_cast<int32>(std::floor((maxW - origin) / step - offset));
			return r;
		}

		/// Calls body(target, row) for every row in [0, rows), each row exactly once, on one thread per target (see
		/// ParallelFor), so no target is called from two threads at once; returns when every row is done.
		void ForEachRow(const std::vector<ChannelTerrainTargetFunction>& targets, const int32 rows,
			const std::function<void(const ChannelTerrainTargetFunction& target, int32 row)>& body)
		{
			ParallelFor(static_cast<size_t>(std::max(0, rows)), targets.size(), [&](const size_t worker, const size_t row)
			{
				body(targets[worker], static_cast<int32>(row));
			});
		}

		float Lerp(float a, float b, float t)
		{
			return a + (b - a) * t;
		}

		/// Normalises non negative weights to a byte sum of 255 (largest remainder, ties to lower index).
		std::array<uint8, 4> Normalize(const std::array<double, 4>& w, const std::array<uint8, 4>& fallback)
		{
			double sum = 0.0;
			for (double v : w)
			{
				sum += std::max(0.0, v);
			}
			if (sum <= 0.0)
			{
				return fallback;
			}

			std::array<uint8, 4> out{};
			std::array<double, 4> frac{};
			int32 total = 0;
			for (size_t i = 0; i < 4; ++i)
			{
				const double scaled = std::max(0.0, w[i]) * 255.0 / sum;
				const double fl = std::floor(scaled);
				out[i] = static_cast<uint8>(fl);
				frac[i] = scaled - fl;
				total += out[i];
			}

			while (total < 255)
			{
				size_t best = 0;
				for (size_t i = 1; i < 4; ++i)
				{
					if (frac[i] > frac[best])
					{
						best = i;
					}
				}
				++out[best];
				frac[best] = -1.0;
				++total;
			}
			return out;
		}

		uint32 Pack(const std::array<uint8, 4>& v)
		{
			return static_cast<uint32>(v[0]) | (static_cast<uint32>(v[1]) << 8) | (static_cast<uint32>(v[2]) << 16) | (static_cast<uint32>(v[3]) << 24);
		}

		std::array<uint8, 4> Unpack(uint32 p)
		{
			return { static_cast<uint8>(p & 0xFF), static_cast<uint8>((p >> 8) & 0xFF), static_cast<uint8>((p >> 16) & 0xFF), static_cast<uint8>((p >> 24) & 0xFF) };
		}

		void Include(IntRect& r, bool& any, int32 x, int32 z)
		{
			if (!any)
			{
				r = IntRect{ x, z, x, z };
				any = true;
				return;
			}
			r.minX = std::min(r.minX, x);
			r.minZ = std::min(r.minZ, z);
			r.maxX = std::max(r.maxX, x);
			r.maxZ = std::max(r.maxZ, z);
		}

		int32 OuterFloor(double world, double origin, double spacing)
		{
			return static_cast<int32>(std::floor((world - origin) / spacing));
		}

		/// Restores height entries: exact base if untouched since, otherwise the user's delta is kept.
		template<typename Get, typename Set>
		void UnapplyHeights(const std::vector<TerrainContribution::Entry<float>>& entries, Get get, Set set)
		{
			for (const auto& e : entries)
			{
				const float current = get(e.x, e.z);
				if (current == e.written)
				{
					set(e.x, e.z, e.base);
				}
				else
				{
					set(e.x, e.z, current - (e.written - e.base));
				}
			}
		}
	}

	float ContributionMatchRatio(const ITerrainAccess& terrain, const TerrainContribution& contribution,
		size_t* matching, size_t* total)
	{
		size_t match = 0;
		for (const auto& e : contribution.outer)
		{
			match += terrain.GetOuterHeight(e.x, e.z) == e.written ? 1 : 0;
		}
		for (const auto& e : contribution.inner)
		{
			match += terrain.GetInnerHeight(e.x, e.z) == e.written ? 1 : 0;
		}

		const size_t count = contribution.outer.size() + contribution.inner.size();
		if (matching)
		{
			*matching = match;
		}
		if (total)
		{
			*total = count;
		}
		return count == 0 ? 1.0f : static_cast<float>(match) / static_cast<float>(count);
	}

	void Unapply(ITerrainAccess& terrain, TerrainContribution& contribution)
	{
		const bool any = !contribution.IsEmpty();

		UnapplyHeights(contribution.outer, [&](int32 x, int32 z) { return terrain.GetOuterHeight(x, z); },
			[&](int32 x, int32 z, float h) { terrain.SetOuterHeight(x, z, h); });
		UnapplyHeights(contribution.inner, [&](int32 x, int32 z) { return terrain.GetInnerHeight(x, z); },
			[&](int32 x, int32 z, float h) { terrain.SetInnerHeight(x, z, h); });
		UnapplyHeights(contribution.waterHeights, [&](int32 x, int32 z) { return terrain.GetWaterHeight(x, z); },
			[&](int32 x, int32 z, float h) { terrain.SetWaterHeight(x, z, h); });

		for (const auto& e : contribution.splat)
		{
			if (terrain.GetSplat(e.x, e.z) == e.written)
			{
				terrain.SetSplat(e.x, e.z, e.base);
			}
		}
		for (const auto& e : contribution.waterQuads)
		{
			if (terrain.GetWaterQuad(e.x, e.z) == e.written)
			{
				terrain.SetWaterQuad(e.x, e.z, e.base);
			}
		}
		for (const auto& e : contribution.materials)
		{
			if (terrain.GetTileMaterial(e.x, e.z) == e.written)
			{
				terrain.SetTileMaterial(e.x, e.z, e.base);
			}
		}

		const IntRect touched = contribution.touchedOuter;
		contribution.Clear();
		if (any)
		{
			terrain.FinishEdits(touched);
		}
	}

	TerrainContribution CaptureSamples(const ITerrainAccess& terrain, const TerrainContribution& contribution)
	{
		TerrainContribution captured = contribution;
		for (auto& e : captured.outer)
		{
			e.written = terrain.GetOuterHeight(e.x, e.z);
		}
		for (auto& e : captured.inner)
		{
			e.written = terrain.GetInnerHeight(e.x, e.z);
		}
		for (auto& e : captured.waterHeights)
		{
			e.written = terrain.GetWaterHeight(e.x, e.z);
		}
		for (auto& e : captured.splat)
		{
			e.written = terrain.GetSplat(e.x, e.z);
		}
		for (auto& e : captured.waterQuads)
		{
			e.written = terrain.GetWaterQuad(e.x, e.z);
		}
		for (auto& e : captured.materials)
		{
			e.written = terrain.GetTileMaterial(e.x, e.z);
		}
		return captured;
	}

	void RestoreSamples(ITerrainAccess& terrain, const TerrainContribution& captured)
	{
		for (const auto& e : captured.outer)
		{
			terrain.SetOuterHeight(e.x, e.z, e.written);
		}
		for (const auto& e : captured.inner)
		{
			terrain.SetInnerHeight(e.x, e.z, e.written);
		}
		for (const auto& e : captured.waterHeights)
		{
			terrain.SetWaterHeight(e.x, e.z, e.written);
		}
		for (const auto& e : captured.splat)
		{
			terrain.SetSplat(e.x, e.z, e.written);
		}
		for (const auto& e : captured.waterQuads)
		{
			terrain.SetWaterQuad(e.x, e.z, e.written);
		}
		for (const auto& e : captured.materials)
		{
			terrain.SetTileMaterial(e.x, e.z, e.written);
		}

		if (!captured.IsEmpty())
		{
			terrain.FinishEdits(captured.touchedOuter);
		}
	}

	void Apply(ITerrainAccess& terrain, const WorldRect& rect, const TerrainTargetFunction& target,
		const String& material, TerrainContribution& out)
	{
		const ChannelTerrainTargetFunction everyChannel = [&target](const float x, const float z, const float baseHeight, terrain_channel::Type)
		{
			return target(x, z, baseHeight);
		};
		Apply(terrain, rect, everyChannel, material, out);
	}

	void Apply(ITerrainAccess& terrain, const WorldRect& rect, const ChannelTerrainTargetFunction& target,
		const String& material, TerrainContribution& out)
	{
		Apply(terrain, rect, std::vector<ChannelTerrainTargetFunction>{ target }, material, out);
	}

	void Apply(ITerrainAccess& terrain, const WorldRect& rect, const std::vector<ChannelTerrainTargetFunction>& targets,
		const String& material, TerrainContribution& out)
	{
		out.Clear();
		if (targets.empty())
		{
			return;
		}

		// Nothing writes the terrain in between, so the bases still hold and the write cannot be refused.
		TerrainSamples samples;
		ComputeTerrainSamples(terrain, rect, targets, samples);
		VERIFY(WriteTerrainSamples(terrain, samples, material, out));
	}

	void ComputeTerrainSamples(const ITerrainAccess& terrain, const WorldRect& rect, const std::vector<ChannelTerrainTargetFunction>& targets,
		TerrainSamples& out)
	{
		out = TerrainSamples();
		if (targets.empty())
		{
			return;
		}
		out.computed = true;

		const GridLayout& l = terrain.GetLayout();
		const double vs = l.vertexSpacing;
		const double quadSize = vs * l.verticesPerTile / l.quadsPerTile;
		const double tileSize = vs * l.verticesPerTile;

		const auto setRange = [](TerrainSamples::Range& range, const IndexRange& rx, const IndexRange& rz)
		{
			range.x0 = rx.lo;
			range.z0 = rz.lo;
			range.width = std::max(0, rx.hi - rx.lo + 1);
			range.rows = range.width > 0 ? std::max(0, rz.hi - rz.lo + 1) : 0;
		};

		// Outer vertices, then inner vertices. Each sample reads only its own base, so the rows are evaluated
		// concurrently with several targets.
		for (int pass = 0; pass < 2; ++pass)
		{
			const double offset = pass == 0 ? 0.0 : 0.5;
			TerrainSamples::Range& range = pass == 0 ? out.outer : out.inner;
			std::vector<TerrainSamples::Height>& samples = pass == 0 ? out.outerSamples : out.innerSamples;
			setRange(range, RangeFor(rect.minX, rect.maxX, l.originX, vs, offset), RangeFor(rect.minZ, rect.maxZ, l.originZ, vs, offset));
			samples.resize(range.Count());
			ForEachRow(targets, range.rows, [&](const ChannelTerrainTargetFunction& target, const int32 row)
			{
				const int32 z = range.z0 + row;
				const double wz = l.originZ + (z + offset) * vs;
				for (int32 i = 0; i < range.width; ++i)
				{
					const int32 x = range.x0 + i;
					const float base = pass == 0 ? terrain.GetOuterHeight(x, z) : terrain.GetInnerHeight(x, z);
					const double wx = l.originX + (x + offset) * vs;
					const TerrainTarget t = target(static_cast<float>(wx), static_cast<float>(wz), base, terrain_channel::Heights);
					samples[range.Index(i, row)] = { base, t.height, t.heightWeight };
				}
			});
		}

		// Splat pixels (the targets do not read the base splat; WriteTerrainSamples blends over it).
		setRange(out.splat, RangeFor(rect.minX, rect.maxX, l.originX, l.pixelSpacing, 0.0), RangeFor(rect.minZ, rect.maxZ, l.originZ, l.pixelSpacing, 0.0));
		out.splatSamples.resize(out.splat.Count());
		ForEachRow(targets, out.splat.rows, [&](const ChannelTerrainTargetFunction& target, const int32 row)
		{
			const int32 z = out.splat.z0 + row;
			const double wz = l.originZ + z * l.pixelSpacing;
			for (int32 i = 0; i < out.splat.width; ++i)
			{
				const double wx = l.originX + (out.splat.x0 + i) * l.pixelSpacing;
				const TerrainTarget t = target(static_cast<float>(wx), static_cast<float>(wz), 0.0f, terrain_channel::Splat);
				out.splatSamples[out.splat.Index(i, row)] = { t.splatWeight, t.splat };
			}
		});

		// Water quads (type and surface height at the quad centre) and tile materials: few samples, the first target.
		const ChannelTerrainTargetFunction& first = targets.front();
		setRange(out.water, RangeFor(rect.minX, rect.maxX, l.originX, quadSize, 0.5), RangeFor(rect.minZ, rect.maxZ, l.originZ, quadSize, 0.5));
		out.waterSamples.resize(out.water.Count());
		for (int32 row = 0; row < out.water.rows; ++row)
		{
			const double wz = l.originZ + (out.water.z0 + row + 0.5) * quadSize;
			for (int32 i = 0; i < out.water.width; ++i)
			{
				const double wx = l.originX + (out.water.x0 + i + 0.5) * quadSize;
				const TerrainTarget t = first(static_cast<float>(wx), static_cast<float>(wz), 0.0f, terrain_channel::Water);
				out.waterSamples[out.water.Index(i, row)] = { t.waterType, t.waterHeight };
			}
		}

		// Tile materials: the tiles whose centre lies in the rect (where setMaterial applies, as it always has) and the
		// tiles the rect overlaps (TileRangeOverlapping, where a named material applies too), in one range.
		const IndexRange centreX = RangeFor(rect.minX, rect.maxX, l.originX, tileSize, 0.5);
		const IndexRange centreZ = RangeFor(rect.minZ, rect.maxZ, l.originZ, tileSize, 0.5);
		const bool anyCentre = centreX.lo <= centreX.hi && centreZ.lo <= centreZ.hi;
		const IntRect overlap = TileRangeOverlapping(l, rect);
		const bool anyOverlap = overlap.minX <= overlap.maxX && overlap.minZ <= overlap.maxZ;
		IndexRange tilesX = centreX;
		IndexRange tilesZ = centreZ;
		if (anyOverlap)
		{
			tilesX = anyCentre ? IndexRange{ std::min(centreX.lo, overlap.minX), std::max(centreX.hi, overlap.maxX) } : IndexRange{ overlap.minX, overlap.maxX };
			tilesZ = anyCentre ? IndexRange{ std::min(centreZ.lo, overlap.minZ), std::max(centreZ.hi, overlap.maxZ) } : IndexRange{ overlap.minZ, overlap.maxZ };
		}
		setRange(out.materials, tilesX, tilesZ);
		out.materialSamples.assign(out.materials.Count(), 0);
		out.materialNames.assign(out.materials.Count(), String());
		for (int32 row = 0; row < out.materials.rows; ++row)
		{
			const int32 z = out.materials.z0 + row;
			const double wz = l.originZ + (z + 0.5) * tileSize;
			for (int32 i = 0; i < out.materials.width; ++i)
			{
				const int32 x = out.materials.x0 + i;
				const bool centred = anyCentre && x >= centreX.lo && x <= centreX.hi && z >= centreZ.lo && z <= centreZ.hi;
				const bool overlapping = anyOverlap && x >= overlap.minX && x <= overlap.maxX && z >= overlap.minZ && z <= overlap.maxZ;
				if (!centred && !overlapping)
				{
					continue;
				}
				const double wx = l.originX + (x + 0.5) * tileSize;
				const TerrainTarget t = first(static_cast<float>(wx), static_cast<float>(wz), 0.0f, terrain_channel::Material);
				out.materialSamples[out.materials.Index(i, row)] = centred && t.setMaterial ? 1 : 0;
				out.materialNames[out.materials.Index(i, row)] = t.material;
			}
		}
	}

	bool WriteTerrainSamples(ITerrainAccess& terrain, const TerrainSamples& samples, const String& material, TerrainContribution& out)
	{
		out.Clear();
		if (!samples.computed)
		{
			return true;
		}

		// The heights are blended over the bases read when they were computed: those must still be the terrain's.
		for (int pass = 0; pass < 2; ++pass)
		{
			const TerrainSamples::Range& range = pass == 0 ? samples.outer : samples.inner;
			const std::vector<TerrainSamples::Height>& heights = pass == 0 ? samples.outerSamples : samples.innerSamples;
			for (int32 row = 0; row < range.rows; ++row)
			{
				for (int32 i = 0; i < range.width; ++i)
				{
					const TerrainSamples::Height& t = heights[range.Index(i, row)];
					if (t.weight <= 0.0f)
					{
						continue;
					}
					const int32 x = range.x0 + i;
					const int32 z = range.z0 + row;
					if ((pass == 0 ? terrain.GetOuterHeight(x, z) : terrain.GetInnerHeight(x, z)) != t.base)
					{
						return false;
					}
				}
			}
		}

		const GridLayout& l = terrain.GetLayout();
		const double vs = l.vertexSpacing;
		const double quadSize = vs * l.verticesPerTile / l.quadsPerTile;
		const double tileSize = vs * l.verticesPerTile;

		bool anyWrite = false;
		IntRect touched{};
		auto touchWorld = [&](double wx, double wz)
		{
			const int32 ox = OuterFloor(wx, l.originX, vs);
			const int32 oz = OuterFloor(wz, l.originZ, vs);
			Include(touched, anyWrite, ox, oz);
			Include(touched, anyWrite, ox + 1, oz + 1);
		};

		// Outer vertices, then inner vertices.
		for (int pass = 0; pass < 2; ++pass)
		{
			const double offset = pass == 0 ? 0.0 : 0.5;
			const TerrainSamples::Range& range = pass == 0 ? samples.outer : samples.inner;
			const std::vector<TerrainSamples::Height>& heights = pass == 0 ? samples.outerSamples : samples.innerSamples;
			auto& list = pass == 0 ? out.outer : out.inner;
			for (int32 row = 0; row < range.rows; ++row)
			{
				const int32 z = range.z0 + row;
				const double wz = l.originZ + (z + offset) * vs;
				for (int32 i = 0; i < range.width; ++i)
				{
					const TerrainSamples::Height& t = heights[range.Index(i, row)];
					if (t.weight <= 0.0f)
					{
						continue;
					}
					const int32 x = range.x0 + i;
					const double wx = l.originX + (x + offset) * vs;
					const float base = t.base;
					const float written = Lerp(base, t.height, std::min(1.0f, t.weight));
					if (written == base)
					{
						continue;
					}
					if (pass == 0)
					{
						terrain.SetOuterHeight(x, z, written);
					}
					else
					{
						terrain.SetInnerHeight(x, z, written);
					}
					list.push_back({ x, z, base, written });
					touchWorld(wx, wz);
				}
			}
		}

		// Splat pixels, blended over the current splat.
		for (int32 row = 0; row < samples.splat.rows; ++row)
		{
			const int32 z = samples.splat.z0 + row;
			for (int32 i = 0; i < samples.splat.width; ++i)
			{
				const TerrainSamples::Splat& t = samples.splatSamples[samples.splat.Index(i, row)];
				if (t.weight <= 0.0f)
				{
					continue;
				}
				const int32 x = samples.splat.x0 + i;
				const double wx = l.originX + x * l.pixelSpacing;
				const double wz = l.originZ + z * l.pixelSpacing;
				const uint32 base = terrain.GetSplat(x, z);
				const std::array<uint8, 4> baseW = Unpack(base);
				const float w = std::min(1.0f, t.weight);
				std::array<double, 4> blended{};
				for (size_t k = 0; k < 4; ++k)
				{
					blended[k] = Lerp(static_cast<float>(baseW[k]), static_cast<float>(t.splat[k]), w);
				}
				const uint32 written = Pack(Normalize(blended, baseW));
				if (written == base)
				{
					continue;
				}
				terrain.SetSplat(x, z, written);
				out.splat.push_back({ x, z, base, written });
				touchWorld(wx, wz);
			}
		}

		// Water quads and the vertex heights of watered quads.
		{
			std::map<std::pair<int32, int32>, float> vertexHeights; // keyed (z, x)
			for (int32 row = 0; row < samples.water.rows; ++row)
			{
				const int32 z = samples.water.z0 + row;
				for (int32 i = 0; i < samples.water.width; ++i)
				{
					const TerrainSamples::Water& t = samples.waterSamples[samples.water.Index(i, row)];
					if (t.type == 0)
					{
						continue;
					}
					const int32 x = samples.water.x0 + i;
					const double wx = l.originX + (x + 0.5) * quadSize;
					const double wz = l.originZ + (z + 0.5) * quadSize;
					const uint8 base = terrain.GetWaterQuad(x, z);
					if (base != t.type)
					{
						terrain.SetWaterQuad(x, z, t.type);
						out.waterQuads.push_back({ x, z, base, t.type });
						touchWorld(wx, wz);
					}
					for (int32 dz = 0; dz <= 1; ++dz)
					{
						for (int32 dx = 0; dx <= 1; ++dx)
						{
							vertexHeights[{ z + dz, x + dx }] = t.height;
						}
					}
				}
			}
			for (const auto& kv : vertexHeights)
			{
				const int32 vz = kv.first.first;
				const int32 vx = kv.first.second;
				const float base = terrain.GetWaterHeight(vx, vz);
				if (base != kv.second)
				{
					terrain.SetWaterHeight(vx, vz, kv.second);
					out.waterHeights.push_back({ vx, vz, base, kv.second });
				}
			}
		}

		// Tile materials.
		for (int32 row = 0; row < samples.materials.rows; ++row)
		{
			const int32 z = samples.materials.z0 + row;
			for (int32 i = 0; i < samples.materials.width; ++i)
			{
				const size_t index = samples.materials.Index(i, row);
				// A named material wins; otherwise the call's material where the target asked for it.
				const bool named = index < samples.materialNames.size() && !samples.materialNames[index].empty();
				if (!named && !samples.materialSamples[index])
				{
					continue;
				}
				const String& tileMaterial = named ? samples.materialNames[index] : material;
				const int32 x = samples.materials.x0 + i;
				const double wx = l.originX + (x + 0.5) * tileSize;
				const double wz = l.originZ + (z + 0.5) * tileSize;
				const String base = terrain.GetTileMaterial(x, z);
				if (base == tileMaterial)
				{
					continue;
				}
				terrain.SetTileMaterial(x, z, tileMaterial);
				out.materials.push_back({ x, z, base, tileMaterial });
				touchWorld(wx, wz);
			}
		}

		if (anyWrite)
		{
			out.touchedOuter = touched;
			terrain.FinishEdits(touched);
		}
		return true;
	}
}
