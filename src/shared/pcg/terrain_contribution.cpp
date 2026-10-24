// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "terrain_contribution.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace mmo::pcg
{
	bool RectsOverlap(const WorldRect& a, const WorldRect& b)
	{
		return a.minX <= b.maxX && b.minX <= a.maxX && a.minZ <= b.maxZ && b.minZ <= a.maxZ;
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

	void Apply(ITerrainAccess& terrain, const WorldRect& rect, const TerrainTargetFunction& target,
		const String& material, TerrainContribution& out)
	{
		out.Clear();

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
			const IndexRange rx = RangeFor(rect.minX, rect.maxX, l.originX, vs, offset);
			const IndexRange rz = RangeFor(rect.minZ, rect.maxZ, l.originZ, vs, offset);
			auto& list = pass == 0 ? out.outer : out.inner;
			for (int32 z = rz.lo; z <= rz.hi; ++z)
			{
				for (int32 x = rx.lo; x <= rx.hi; ++x)
				{
					const float base = pass == 0 ? terrain.GetOuterHeight(x, z) : terrain.GetInnerHeight(x, z);
					const double wx = l.originX + (x + offset) * vs;
					const double wz = l.originZ + (z + offset) * vs;
					const TerrainTarget t = target(static_cast<float>(wx), static_cast<float>(wz), base);
					if (t.heightWeight <= 0.0f)
					{
						continue;
					}
					const float written = Lerp(base, t.height, std::min(1.0f, t.heightWeight));
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

		// Splat pixels.
		{
			const IndexRange rx = RangeFor(rect.minX, rect.maxX, l.originX, l.pixelSpacing, 0.0);
			const IndexRange rz = RangeFor(rect.minZ, rect.maxZ, l.originZ, l.pixelSpacing, 0.0);
			for (int32 z = rz.lo; z <= rz.hi; ++z)
			{
				for (int32 x = rx.lo; x <= rx.hi; ++x)
				{
					const double wx = l.originX + x * l.pixelSpacing;
					const double wz = l.originZ + z * l.pixelSpacing;
					const TerrainTarget t = target(static_cast<float>(wx), static_cast<float>(wz), 0.0f);
					if (t.splatWeight <= 0.0f)
					{
						continue;
					}
					const uint32 base = terrain.GetSplat(x, z);
					const std::array<uint8, 4> baseW = Unpack(base);
					const float w = std::min(1.0f, t.splatWeight);
					std::array<double, 4> blended{};
					for (size_t i = 0; i < 4; ++i)
					{
						blended[i] = Lerp(static_cast<float>(baseW[i]), static_cast<float>(t.splat[i]), w);
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
		}

		// Water quads and the vertex heights of watered quads.
		{
			std::map<std::pair<int32, int32>, float> vertexHeights; // keyed (z, x)
			const IndexRange rx = RangeFor(rect.minX, rect.maxX, l.originX, quadSize, 0.5);
			const IndexRange rz = RangeFor(rect.minZ, rect.maxZ, l.originZ, quadSize, 0.5);
			for (int32 z = rz.lo; z <= rz.hi; ++z)
			{
				for (int32 x = rx.lo; x <= rx.hi; ++x)
				{
					const double wx = l.originX + (x + 0.5) * quadSize;
					const double wz = l.originZ + (z + 0.5) * quadSize;
					const TerrainTarget t = target(static_cast<float>(wx), static_cast<float>(wz), 0.0f);
					if (t.waterType == 0)
					{
						continue;
					}
					const uint8 base = terrain.GetWaterQuad(x, z);
					if (base != t.waterType)
					{
						terrain.SetWaterQuad(x, z, t.waterType);
						out.waterQuads.push_back({ x, z, base, t.waterType });
						touchWorld(wx, wz);
					}
					for (int32 dz = 0; dz <= 1; ++dz)
					{
						for (int32 dx = 0; dx <= 1; ++dx)
						{
							vertexHeights[{ z + dz, x + dx }] = t.waterHeight;
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
		{
			const IndexRange rx = RangeFor(rect.minX, rect.maxX, l.originX, tileSize, 0.5);
			const IndexRange rz = RangeFor(rect.minZ, rect.maxZ, l.originZ, tileSize, 0.5);
			for (int32 z = rz.lo; z <= rz.hi; ++z)
			{
				for (int32 x = rx.lo; x <= rx.hi; ++x)
				{
					const double wx = l.originX + (x + 0.5) * tileSize;
					const double wz = l.originZ + (z + 0.5) * tileSize;
					const TerrainTarget t = target(static_cast<float>(wx), static_cast<float>(wz), 0.0f);
					if (!t.setMaterial)
					{
						continue;
					}
					const String base = terrain.GetTileMaterial(x, z);
					if (base == material)
					{
						continue;
					}
					terrain.SetTileMaterial(x, z, material);
					out.materials.push_back({ x, z, base, material });
					touchWorld(wx, wz);
				}
			}
		}

		if (anyWrite)
		{
			out.touchedOuter = touched;
			terrain.FinishEdits(touched);
		}
	}
}
