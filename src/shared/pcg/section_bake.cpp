// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "section_bake.h"

#include "nlohmann/json.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <cstdio>
#include <cstdlib>

namespace mmo::pcg
{
	namespace
	{
		using Json = nlohmann::json;

		constexpr double kFloatLimit = 3.0e38;
		constexpr double kSnapEpsilon = 1.0e-6;
		/// Tolerance for float rounding when counting grid steps.
		constexpr double kGridEpsilon = 1.0e-4;

		double Lerp(const double a, const double b, const double t)
		{
			return a + (b - a) * t;
		}

		/// Splits a position in vertex space into a cell index and a fraction, snapping fractions
		/// within epsilon of a vertex onto it so exact positions never read neighbouring cells.
		void SplitCell(const double u, int32& index, double& fraction)
		{
			double whole = std::floor(u);
			double frac = u - whole;
			if (frac < kSnapEpsilon)
			{
				frac = 0.0;
			}
			else if (frac > 1.0 - kSnapEpsilon)
			{
				whole += 1.0;
				frac = 0.0;
			}
			index = static_cast<int32>(whole);
			fraction = frac;
		}

		struct Extent
		{
			int32 first;
			int32 count;
		};

		/// Grid indices i with i * spacing in [lo, hi].
		Extent GridExtent(const double lo, const double hi, const double spacing)
		{
			const int32 first = static_cast<int32>(std::ceil(lo / spacing - kGridEpsilon));
			const int32 last = static_cast<int32>(std::floor(hi / spacing + kGridEpsilon));
			return { first, std::max(0, last - first + 1) };
		}

		/// Shortest decimal that reads back as the same float, as a double that prints compactly.
		double PrettyFloat(const float value)
		{
			char buffer[32];
			for (int precision = 1; precision <= 9; ++precision)
			{
				std::snprintf(buffer, sizeof(buffer), "%.*g", precision, static_cast<double>(value));
				if (std::strtof(buffer, nullptr) == value)
				{
					break;
				}
			}
			return std::strtod(buffer, nullptr);
		}

		void ReadFloat(const Json& obj, const char* key, float& out)
		{
			const auto it = obj.find(key);
			if (it != obj.end() && it->is_number())
			{
				const double value = it->get<double>();
				out = static_cast<float>(std::max(-kFloatLimit, std::min(kFloatLimit, value)));
			}
		}

		void ReadString(const Json& obj, const char* key, String& out)
		{
			const auto it = obj.find(key);
			if (it != obj.end() && it->is_string())
			{
				out = it->get<String>();
			}
		}
	}

	float SampleTerrainHeight(const ITerrainAccess& terrain, const double worldX, const double worldZ)
	{
		const GridLayout& layout = terrain.GetLayout();
		int32 ix = 0, iz = 0;
		double fx = 0.0, fz = 0.0;
		SplitCell((worldX - layout.originX) / layout.vertexSpacing, ix, fx);
		SplitCell((worldZ - layout.originZ) / layout.vertexSpacing, iz, fz);

		if (fx == 0.0 && fz == 0.0)
		{
			return terrain.GetOuterHeight(ix, iz);
		}
		if (fz == 0.0)
		{
			return static_cast<float>(Lerp(terrain.GetOuterHeight(ix, iz), terrain.GetOuterHeight(ix + 1, iz), fx));
		}
		if (fx == 0.0)
		{
			return static_cast<float>(Lerp(terrain.GetOuterHeight(ix, iz), terrain.GetOuterHeight(ix, iz + 1), fz));
		}

		const double h00 = terrain.GetOuterHeight(ix, iz);
		const double h10 = terrain.GetOuterHeight(ix + 1, iz);
		const double h01 = terrain.GetOuterHeight(ix, iz + 1);
		const double h11 = terrain.GetOuterHeight(ix + 1, iz + 1);
		const double hc = terrain.GetInnerHeight(ix, iz);

		// Four triangles around the centre; the diagonals through the centre split the cell.
		const double dx = fx - 0.5;
		const double dz = fz - 0.5;
		const double adx = std::abs(dx);
		const double adz = std::abs(dz);
		if (adx <= adz)
		{
			if (adz == 0.0)
			{
				return static_cast<float>(hc);
			}
			const double ha = dz < 0.0 ? h00 : h01;
			const double hb = dz < 0.0 ? h10 : h11;
			const double edge = Lerp(ha, hb, 0.5 + dx * (0.5 / adz));
			return static_cast<float>(Lerp(hc, edge, adz / 0.5));
		}

		const double ha = dx < 0.0 ? h00 : h10;
		const double hb = dx < 0.0 ? h01 : h11;
		const double edge = Lerp(ha, hb, 0.5 + dz * (0.5 / adx));
		return static_cast<float>(Lerp(hc, edge, adx / 0.5));
	}

	bool BakeSection(const SectionDefinition& def, const ITerrainAccess& terrain, const std::vector<Placement>& worldPlacements,
		const uint32 defaultPaint, const std::array<String, 4>& layerTextures, const uint64 sourceStamp, SectionAsset& out, String& error,
		size_t* ignoredPlacements)
	{
		error.clear();
		out = SectionAsset();
		if (ignoredPlacements != nullptr)
		{
			*ignoredPlacements = 0;
		}

		if (def.variants.empty())
		{
			error = "The section has no variants.";
			return false;
		}
		if (!(def.length > 0.0f) || !(def.halfWidth > 0.0f) || !(def.sideFalloff >= 0.0f))
		{
			error = "Length and half width must be positive and the side falloff must not be negative.";
			return false;
		}

		const GridLayout& layout = terrain.GetLayout();
		if (!(layout.vertexSpacing > 0.0) || !(layout.pixelSpacing > 0.0))
		{
			error = "The terrain layout has a non-positive spacing.";
			return false;
		}

		const float extent = def.halfWidth + def.sideFalloff;
		for (size_t i = 0; i < def.variants.size(); ++i)
		{
			for (size_t j = i + 1; j < def.variants.size(); ++j)
			{
				if (std::abs(def.variants[i].originX - def.variants[j].originX) < 2.0f * extent)
				{
					error = "Variants " + std::to_string(i) + " and " + std::to_string(j) + " overlap: their capture rectangles are "
						+ std::to_string(2.0f * extent) + " m wide, so origins must be at least that far apart.";
					return false;
				}
			}
		}

		out.name = def.name;
		out.length = def.length;
		out.halfWidth = def.halfWidth;
		out.sideFalloff = def.sideFalloff;
		out.layerTextures = layerTextures;
		out.sourceStamp = sourceStamp;
		out.rules = def.rules;
		std::stable_sort(out.rules.begin(), out.rules.end(), [](const PropRule& a, const PropRule& b) { return a.asset < b.asset; });

		const double vs = layout.vertexSpacing;
		const double ps = layout.pixelSpacing;
		const Extent hx = GridExtent(-extent, extent, vs);
		const Extent hz = GridExtent(0.0, def.length, vs);
		const Extent px = GridExtent(-extent, extent, ps);
		const Extent pz = GridExtent(0.0, def.length, ps);

		out.variants.reserve(def.variants.size());
		for (const SectionVariantDef& vd : def.variants)
		{
			SectionVariant v;
			v.originX = vd.originX;

			v.heights.spacing = static_cast<float>(vs);
			v.heights.originX = hx.first;
			v.heights.originZ = hz.first;
			v.heights.width = static_cast<uint32>(hx.count);
			v.heights.depth = static_cast<uint32>(hz.count);
			v.heights.values.reserve(static_cast<size_t>(hx.count) * hz.count);
			for (int32 j = 0; j < hz.count; ++j)
			{
				for (int32 i = 0; i < hx.count; ++i)
				{
					const double wx = static_cast<double>(vd.originX) + (hx.first + i) * vs;
					const double wz = (hz.first + j) * vs;
					v.heights.values.push_back(SampleTerrainHeight(terrain, wx, wz));
				}
			}

			v.paint.spacing = static_cast<float>(ps);
			v.paint.originX = px.first;
			v.paint.originZ = pz.first;
			v.paint.width = static_cast<uint32>(px.count);
			v.paint.depth = static_cast<uint32>(pz.count);
			v.paint.packed.reserve(static_cast<size_t>(px.count) * pz.count);
			v.paint.coverage.reserve(static_cast<size_t>(px.count) * pz.count);
			for (int32 j = 0; j < pz.count; ++j)
			{
				for (int32 i = 0; i < px.count; ++i)
				{
					const double wx = static_cast<double>(vd.originX) + (px.first + i) * ps;
					const double wz = (pz.first + j) * ps;
					const int32 pixelX = static_cast<int32>(std::floor((wx - layout.originX) / ps + 0.5));
					const int32 pixelZ = static_cast<int32>(std::floor((wz - layout.originZ) / ps + 0.5));
					const uint32 packed = terrain.GetSplat(pixelX, pixelZ);
					v.paint.packed.push_back(packed);
					v.paint.coverage.push_back(packed != defaultPaint ? 255 : 0);
				}
			}

			out.variants.push_back(std::move(v));
		}

		size_t ignored = 0;
		for (const Placement& placement : worldPlacements)
		{
			const float wx = placement.position.x;
			const float wz = placement.position.z;
			size_t owner = out.variants.size();
			if (wz >= 0.0f && wz <= def.length)
			{
				for (size_t i = 0; i < def.variants.size(); ++i)
				{
					if (std::abs(wx - def.variants[i].originX) <= extent)
					{
						owner = i;
						break;
					}
				}
			}
			if (owner == out.variants.size())
			{
				++ignored;
				continue;
			}

			SectionPlacement sp;
			sp.placement = placement;
			sp.placement.position.x = wx - def.variants[owner].originX;
			sp.heightAboveTerrain = placement.position.y - SampleTerrainHeight(terrain, wx, wz);
			out.variants[owner].placements.push_back(std::move(sp));
		}

		if (ignoredPlacements != nullptr)
		{
			*ignoredPlacements = ignored;
		}
		return true;
	}

	void ClampPropRule(PropRule& rule)
	{
		rule.keepChance = std::clamp(rule.keepChance, 0.0f, 1.0f);
		rule.positionJitter = std::max(0.0f, rule.positionJitter);
		rule.yawJitterDeg = std::max(0.0f, rule.yawJitterDeg);
		rule.scaleJitter = std::max(0.0f, rule.scaleJitter);
	}

	bool ParseSectionDefinition(const String& json, SectionDefinition& out)
	{
		const Json root = Json::parse(json, nullptr, false);
		if (root.is_discarded() || !root.is_object())
		{
			return false;
		}

		out = SectionDefinition();
		ReadString(root, "name", out.name);
		ReadFloat(root, "length", out.length);
		ReadFloat(root, "halfWidth", out.halfWidth);
		ReadFloat(root, "sideFalloff", out.sideFalloff);

		const auto vit = root.find("variants");
		if (vit != root.end() && vit->is_array())
		{
			for (const Json& item : *vit)
			{
				if (item.is_object())
				{
					SectionVariantDef v;
					ReadFloat(item, "originX", v.originX);
					out.variants.push_back(v);
				}
			}
		}

		const auto rit = root.find("rules");
		if (rit != root.end() && rit->is_array())
		{
			for (const Json& item : *rit)
			{
				if (!item.is_object())
				{
					continue;
				}
				PropRule rule;
				ReadString(item, "asset", rule.asset);
				if (rule.asset.empty())
				{
					continue;
				}
				ReadFloat(item, "keepChance", rule.keepChance);
				ReadFloat(item, "positionJitter", rule.positionJitter);
				ReadFloat(item, "yawJitterDeg", rule.yawJitterDeg);
				ReadFloat(item, "scaleJitter", rule.scaleJitter);
				ClampPropRule(rule);
				const auto sit = item.find("swapGroup");
				if (sit != item.end() && sit->is_array())
				{
					for (const Json& s : *sit)
					{
						if (s.is_string())
						{
							rule.swapGroup.push_back(s.get<String>());
						}
					}
				}
				out.rules.push_back(std::move(rule));
			}
		}
		return true;
	}

	String WriteSectionDefinition(const SectionDefinition& def)
	{
		// nlohmann objects keep their keys sorted, which makes the output deterministic.
		Json root = Json::object();
		root["name"] = def.name;
		root["length"] = PrettyFloat(def.length);
		root["halfWidth"] = PrettyFloat(def.halfWidth);
		root["sideFalloff"] = PrettyFloat(def.sideFalloff);

		Json variants = Json::array();
		for (const SectionVariantDef& v : def.variants)
		{
			Json item = Json::object();
			item["originX"] = PrettyFloat(v.originX);
			variants.push_back(std::move(item));
		}
		root["variants"] = std::move(variants);

		Json rules = Json::array();
		for (const PropRule& rule : def.rules)
		{
			Json item = Json::object();
			item["asset"] = rule.asset;
			item["keepChance"] = PrettyFloat(rule.keepChance);
			item["positionJitter"] = PrettyFloat(rule.positionJitter);
			item["yawJitterDeg"] = PrettyFloat(rule.yawJitterDeg);
			item["scaleJitter"] = PrettyFloat(rule.scaleJitter);
			Json swap = Json::array();
			for (const String& s : rule.swapGroup)
			{
				swap.push_back(s);
			}
			item["swapGroup"] = std::move(swap);
			rules.push_back(std::move(item));
		}
		root["rules"] = std::move(rules);

		return root.dump(2);
	}

	String CanonicalSectionDefinitionText(const String& json)
	{
		SectionDefinition def;
		if (ParseSectionDefinition(json, def))
		{
			return WriteSectionDefinition(def);
		}

		String stripped;
		stripped.reserve(json.size());
		for (const char c : json)
		{
			if (c != '\r')
			{
				stripped.push_back(c);
			}
		}
		return stripped;
	}
}
