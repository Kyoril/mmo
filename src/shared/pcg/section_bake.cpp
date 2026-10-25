// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "section_bake.h"

#include "log/default_log_levels.h"
#include "nlohmann/json.hpp"

#include <algorithm>
#include <map>
#include <set>
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

		constexpr float kDefaultCornerAngleDeg = 35.0f;

		const char* RoleName(const SectionVariantRole role)
		{
			switch (role)
			{
			case SectionVariantRole::InnerCorner: return "innerCorner";
			case SectionVariantRole::OuterCorner: return "outerCorner";
			default: return "straight";
			}
		}

		SectionVariantRole ParseRole(const String& name)
		{
			if (name == "innerCorner")
			{
				return SectionVariantRole::InnerCorner;
			}
			if (name == "outerCorner")
			{
				return SectionVariantRole::OuterCorner;
			}
			if (name != "straight")
			{
				WLOG("Section definition: unknown variant role '" << name << "', read as straight (known: straight, innerCorner, outerCorner)");
			}
			return SectionVariantRole::Straight;
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
		return BakeSection(def, terrain, worldPlacements, {}, defaultPaint, layerTextures, sourceStamp, out, error, ignoredPlacements);
	}

	bool BakeSection(const SectionDefinition& def, const ITerrainAccess& terrain, const std::vector<Placement>& worldPlacements,
		const std::vector<SectionPieceExtent>& pieceExtents, const uint32 defaultPaint, const std::array<String, 4>& layerTextures,
		const uint64 sourceStamp, SectionAsset& out, String& error, size_t* ignoredPlacements, size_t* missingMembers)
	{
		error.clear();
		out = SectionAsset();
		if (ignoredPlacements != nullptr)
		{
			*ignoredPlacements = 0;
		}
		if (missingMembers != nullptr)
		{
			*missingMembers = 0;
		}

		// Run membership: member key -> run index. A map entity may belong to one run only.
		std::map<uint64, size_t> memberRun;
		for (size_t r = 0; r < def.runs.size(); ++r)
		{
			if (def.runs[r].name.empty())
			{
				error = "Run " + std::to_string(r + 1) + " has no name.";
				return false;
			}
			for (const uint64 key : def.runs[r].members)
			{
				const auto inserted = memberRun.emplace(key, r);
				if (!inserted.second && inserted.first->second != r)
				{
					error = "Map entity " + std::to_string(key) + " is a member of the runs '" + def.runs[inserted.first->second].name + "' and '" +
						def.runs[r].name + "'; a placement may belong to one run only.";
					return false;
				}
			}
		}

		std::vector<SectionPieceExtent> extents = pieceExtents;
		std::sort(extents.begin(), extents.end(), [](const SectionPieceExtent& a, const SectionPieceExtent& b) { return a.key < b.key; });

		if (def.variants.empty())
		{
			error = "The section has no variants.";
			return false;
		}
		if (std::none_of(def.variants.begin(), def.variants.end(), [](const SectionVariantDef& v) { return v.role == SectionVariantRole::Straight; }))
		{
			error = "The section has no straight variant (a path lays its straight copies from them); set at least one variant's role to straight.";
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
		out.cornerAngleDeg = def.cornerAngleDeg;
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
			v.role = vd.role;

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

		constexpr size_t NoVariant = static_cast<size_t>(-1);
		std::vector<std::vector<SectionRunPiece>> runPieces(def.runs.size());
		std::vector<size_t> runVariant(def.runs.size(), NoVariant);
		std::set<uint64> foundMembers;

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
			const auto member = placement.kind != PlacementKind::Foliage ? memberRun.find(placement.key) : memberRun.end();
			if (member != memberRun.end())
			{
				const size_t r = member->second;
				const String& name = def.runs[r].name;
				if (owner == out.variants.size())
				{
					out = SectionAsset();
					error = "Run '" + name + "': member " + std::to_string(placement.key) + " lies outside every variant.";
					return false;
				}
				if (runVariant[r] != NoVariant && runVariant[r] != owner)
				{
					out = SectionAsset();
					error = "Run '" + name + "' has members in variant " + std::to_string(runVariant[r] + 1) + " and variant " +
						std::to_string(owner + 1) + "; a run belongs to one variant.";
					return false;
				}

				const auto it = std::lower_bound(extents.begin(), extents.end(), placement.key,
					[](const SectionPieceExtent& e, const uint64 key) { return e.key < key; });
				if (it == extents.end() || it->key != placement.key || !(it->length >= kMinRunPieceLength))
				{
					out = SectionAsset();
					error = "Run '" + name + "': member " + std::to_string(placement.key) + " (" + placement.asset +
						") has no length along the run (needs at least " + std::to_string(kMinRunPieceLength) + " m).";
					return false;
				}

				runVariant[r] = owner;
				SectionRunPiece piece;
				piece.placement = placement;
				piece.placement.position.x = wx - def.variants[owner].originX;
				piece.heightAboveTerrain = placement.position.y - SampleTerrainHeight(terrain, wx, wz);
				piece.length = it->length;
				piece.startOffset = it->startOffset;
				runPieces[r].push_back(std::move(piece));
				foundMembers.insert(placement.key);
				continue;
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

		// Runs in definition order; a run without any member in the world is dropped.
		for (size_t r = 0; r < def.runs.size(); ++r)
		{
			std::vector<SectionRunPiece>& pieces = runPieces[r];
			if (pieces.empty())
			{
				continue;
			}

			SectionVariant& variant = out.variants[runVariant[r]];
			const SectionRunDef& runDef = def.runs[r];
			const bool duplicate = std::any_of(variant.runs.begin(), variant.runs.end(), [&runDef](const SectionRun& other) { return other.name == runDef.name; });
			if (duplicate)
			{
				out = SectionAsset();
				error = "Variant " + std::to_string(runVariant[r] + 1) + " has two runs named '" + runDef.name + "'; run names must be unique per variant.";
				return false;
			}

			std::sort(pieces.begin(), pieces.end(), [](const SectionRunPiece& a, const SectionRunPiece& b)
			{
				if (a.placement.position.z != b.placement.position.z)
				{
					return a.placement.position.z < b.placement.position.z;
				}
				return a.placement.key < b.placement.key;
			});

			SectionRun run;
			run.name = runDef.name;
			run.keepChance = std::clamp(runDef.keepChance, 0.0f, 1.0f);
			double sumX = 0.0;
			for (size_t i = 0; i < pieces.size(); ++i)
			{
				const SectionRunPiece& piece = pieces[i];
				const float start = piece.placement.position.z + piece.startOffset;
				const float end = start + piece.length;
				sumX += piece.placement.position.x;
				run.startZ = i == 0 ? start : std::min(run.startZ, start);
				run.endZ = i == 0 ? end : std::max(run.endZ, end);
			}
			run.x = static_cast<float>(sumX / static_cast<double>(pieces.size()));
			run.pieces = std::move(pieces);
			variant.runs.push_back(std::move(run));
		}

		if (ignoredPlacements != nullptr)
		{
			*ignoredPlacements = ignored;
		}
		if (missingMembers != nullptr)
		{
			*missingMembers = memberRun.size() - foundMembers.size();
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
		ReadFloat(root, "cornerAngleDeg", out.cornerAngleDeg);
		out.cornerAngleDeg = std::clamp(out.cornerAngleDeg, 0.0f, 180.0f);

		const auto vit = root.find("variants");
		if (vit != root.end() && vit->is_array())
		{
			for (const Json& item : *vit)
			{
				if (item.is_object())
				{
					SectionVariantDef v;
					ReadFloat(item, "originX", v.originX);
					const auto roleIt = item.find("role");
					if (roleIt != item.end() && roleIt->is_string())
					{
						v.role = ParseRole(roleIt->get<String>());
					}
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

		const auto runIt = root.find("runs");
		if (runIt != root.end() && runIt->is_array())
		{
			for (const Json& item : *runIt)
			{
				if (!item.is_object())
				{
					continue;
				}
				SectionRunDef run;
				ReadString(item, "name", run.name);
				ReadFloat(item, "keepChance", run.keepChance);
				run.keepChance = std::clamp(run.keepChance, 0.0f, 1.0f);
				const auto mit = item.find("members");
				if (mit != item.end() && mit->is_array())
				{
					for (const Json& m : *mit)
					{
						if (m.is_number_unsigned())
						{
							run.members.push_back(m.get<uint64>());
						}
					}
				}
				std::sort(run.members.begin(), run.members.end());
				run.members.erase(std::unique(run.members.begin(), run.members.end()), run.members.end());
				out.runs.push_back(std::move(run));
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
		if (def.cornerAngleDeg != kDefaultCornerAngleDeg)
		{
			root["cornerAngleDeg"] = PrettyFloat(def.cornerAngleDeg);
		}

		Json variants = Json::array();
		for (const SectionVariantDef& v : def.variants)
		{
			Json item = Json::object();
			item["originX"] = PrettyFloat(v.originX);
			if (v.role != SectionVariantRole::Straight)
			{
				item["role"] = RoleName(v.role);
			}
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

		// Only written when there are runs, so definitions without runs keep their text and source stamp.
		if (!def.runs.empty())
		{
			Json runs = Json::array();
			for (const SectionRunDef& run : def.runs)
			{
				Json item = Json::object();
				item["name"] = run.name;
				item["keepChance"] = PrettyFloat(run.keepChance);
				Json members = Json::array();
				for (const uint64 member : run.members)
				{
					members.push_back(member);
				}
				item["members"] = std::move(members);
				runs.push_back(std::move(item));
			}
			root["runs"] = std::move(runs);
		}

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
