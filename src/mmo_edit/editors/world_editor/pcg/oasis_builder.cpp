// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "oasis_builder.h"

#include "editor_terrain_access.h"
#include "editors/world_editor/edit_modes/foliage_edit_mode.h"
#include "editors/world_editor/edit_modes/world_edit_mode.h"
#include "log/default_log_levels.h"
#include "math/vector3.h"
#include "pcg/placement_reconcile.h"
#include "scene_graph/instanced_foliage.h"
#include "terrain/constants.h"
#include "terrain/terrain.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <sstream>

namespace mmo
{
	namespace
	{
		constexpr float RadToDeg = 57.2957795131f;

		/// Category of every map entity an oasis creates.
		const String PcgCategory = "Pcg";

		using Clock = std::chrono::steady_clock;

		double MillisecondsSince(const Clock::time_point start)
		{
			return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
		}

		const char* DescribeError(const pcg::OasisError error)
		{
			switch (error)
			{
			case pcg::OasisError::None:
				return "no error";
			case pcg::OasisError::TooFewPoints:
				return "the loop needs at least 3 points";
			case pcg::OasisError::ZeroArea:
				return "the loop encloses no area";
			case pcg::OasisError::SelfIntersecting:
				return "the loop intersects itself";
			}

			return "unknown error";
		}

		/// "Oasis <oasis id hex> <placement key hex>", both as 16 lower case hex digits.
		String MakeEntityName(const uint64 oasisId, const uint64 key)
		{
			char buffer[64];
			std::snprintf(buffer, sizeof(buffer), "Oasis %016llx %016llx",
				static_cast<unsigned long long>(oasisId), static_cast<unsigned long long>(key));
			return String(buffer);
		}

		bool IsWorldModelAsset(const String& asset)
		{
			static const String extension = ".hwmo";
			if (asset.size() < extension.size())
			{
				return false;
			}

			return std::equal(extension.begin(), extension.end(), asset.end() - static_cast<std::ptrdiff_t>(extension.size()),
				[](const char a, const char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
		}

		void Include(pcg::WorldRect& rect, bool& any, const pcg::WorldRect& other)
		{
			if (!any)
			{
				rect = other;
				any = true;
				return;
			}

			rect.minX = std::min(rect.minX, other.minX);
			rect.minZ = std::min(rect.minZ, other.minZ);
			rect.maxX = std::max(rect.maxX, other.maxX);
			rect.maxZ = std::max(rect.maxZ, other.maxZ);
		}

		/// Whether the record has anything in the world (a contribution or owned objects).
		bool HasContent(const pcg::OasisRecord& record)
		{
			return !record.terrain.IsEmpty() || !record.placements.empty();
		}

		/// The old influence plus every owned object's position: everything that has to be loaded
		/// to undo the record's previous build.
		bool GetOldContentRect(const pcg::OasisRecord& record, pcg::WorldRect& out)
		{
			bool any = false;
			if (!record.terrain.IsEmpty())
			{
				Include(out, any, record.influence);
			}

			for (const auto& owned : record.placements)
			{
				const Vector3& p = owned.written.position;
				Include(out, any, pcg::WorldRect{ p.x, p.z, p.x, p.z });
			}

			return any;
		}

		/// World extent of the terrain.
		pcg::WorldRect GetTerrainRect(const terrain::Terrain& terrain)
		{
			const auto halfX = static_cast<float>(static_cast<double>(terrain.GetWidth()) / 2.0 * terrain::constants::PageSize);
			const auto halfZ = static_cast<float>(static_cast<double>(terrain.GetHeight()) / 2.0 * terrain::constants::PageSize);
			return pcg::WorldRect{ -halfX, -halfZ, halfX, halfZ };
		}

		bool Contains(const pcg::WorldRect& outer, const pcg::WorldRect& inner)
		{
			return inner.minX >= outer.minX && inner.minZ >= outer.minZ && inner.maxX <= outer.maxX && inner.maxZ <= outer.maxZ;
		}

		uint32 CountSamples(const pcg::TerrainContribution& contribution)
		{
			return static_cast<uint32>(contribution.outer.size() + contribution.inner.size() + contribution.waterHeights.size() +
				contribution.splat.size() + contribution.waterQuads.size() + contribution.materials.size());
		}

		void ReportTouchedPages(const EditorTerrainAccess& access, OasisBuildReport& report)
		{
			report.touchedPages.clear();
			for (const auto& page : access.GetTouchedPages())
			{
				report.touchedPages.push_back(static_cast<uint16>((page.first << 8) | page.second));
			}

			if (report.touchedPages.empty())
			{
				return;
			}

			std::ostringstream pages;
			for (const auto& page : access.GetTouchedPages())
			{
				pages << " " << page.first << "x" << page.second;
			}
			ILOG("PCG: terrain pages changed (their .tlod bake is stale, run --bake-terrain-lod):" << pages.str());
		}

		/// Loads every page the rect touches, widened by two vertex spacings so that seam copies of
		/// vertices and pixels on the rect border (which live in the neighbour page) are loaded too.
		/// On failure the report gets the error (naming the pages) and nothing may be applied.
		bool LoadPagesOrFail(IWorldEditor& editor, const pcg::WorldRect& rect, OasisBuildReport& report)
		{
			const auto margin = static_cast<float>(2.0 * terrain::constants::PageSize / static_cast<double>(terrain::constants::OuterVerticesPerPageSide - 1));
			String error;
			if (editor.EnsurePagesLoaded(rect.minX - margin, rect.minZ - margin, rect.maxX + margin, rect.maxZ + margin, error))
			{
				return true;
			}

			report.error = "Refused, the world was not changed: " + error;
			ELOG("PCG: " << report.error);
			return false;
		}

		bool FoliageExists(const InstancedFoliage& foliage, const uint64 uniqueId)
		{
			InstancedFoliageInstance instance;
			return foliage.TryGetInstance(uniqueId, instance);
		}

		/// Deletes an owned object. Returns false if it no longer exists.
		bool RemoveOwned(IWorldEditor& editor, const uint64 uniqueId, const pcg::PlacementKind kind)
		{
			if (kind == pcg::PlacementKind::Entity)
			{
				return editor.RemoveMapEntityById(uniqueId);
			}

			InstancedFoliage& foliage = editor.GetInstancedFoliage();
			InstancedFoliageInstance instance;
			if (!foliage.TryGetInstance(uniqueId, instance))
			{
				return false;
			}

			editor.MarkFoliagePageDirty(instance.pageIndex);
			return foliage.RemoveInstance(uniqueId);
		}

		/// Creates the world object of a placement. Returns its unique id, or 0 on failure.
		uint64 CreatePlacement(IWorldEditor& editor, const uint64 oasisId, const pcg::Placement& placement)
		{
			if (placement.kind == pcg::PlacementKind::Entity)
			{
				const uint64 uniqueId = editor.GenerateEntityUniqueId();
				const bool created = IsWorldModelAsset(placement.asset)
					? editor.CreateWorldModelEntity(placement.asset, placement.position, placement.rotation, placement.scale, uniqueId) != nullptr
					: editor.CreateMapEntity(placement.asset, placement.position, placement.rotation, placement.scale, uniqueId) != nullptr;
				if (!created || !editor.HasMapEntity(uniqueId))
				{
					WLOG("PCG: failed to create entity " << placement.asset);
					return 0;
				}

				editor.SetMapEntityInfo(uniqueId, MakeEntityName(oasisId, placement.key), PcgCategory);
				return uniqueId;
			}

			InstancedFoliageInstance instance;
			instance.uniqueId = editor.GenerateFoliageUniqueId();
			instance.meshName = placement.asset;
			instance.position = placement.position;
			instance.rotation = placement.rotation;
			instance.scale = placement.scale;
			instance.pageIndex = FoliageEditMode::PageIndexFromPosition(placement.position);
			instance.collides = placement.collides;
			if (!editor.GetInstancedFoliage().AddInstance(instance))
			{
				WLOG("PCG: failed to add foliage instance " << placement.asset);
				return 0;
			}

			editor.MarkFoliagePageDirty(instance.pageIndex);
			return instance.uniqueId;
		}

		/// Moves an owned object to the transform of owned.written. Returns false if it is gone.
		bool UpdatePlacement(IWorldEditor& editor, const pcg::OwnedPlacement& owned)
		{
			const pcg::Placement& p = owned.written;
			if (owned.kind == pcg::PlacementKind::Entity)
			{
				return editor.SetMapEntityTransform(owned.uniqueId, p.position, p.rotation, p.scale);
			}

			InstancedFoliage& foliage = editor.GetInstancedFoliage();
			InstancedFoliageInstance instance;
			if (!foliage.TryGetInstance(owned.uniqueId, instance))
			{
				return false;
			}

			// Foliage is saved per page by its pageIndex, which UpdateInstanceTransform does not change:
			// an instance that moves to another page is re-added under the new page.
			const uint16 newPage = FoliageEditMode::PageIndexFromPosition(p.position);
			editor.MarkFoliagePageDirty(instance.pageIndex);
			editor.MarkFoliagePageDirty(newPage);
			if (instance.pageIndex == newPage && instance.meshName == p.asset)
			{
				foliage.UpdateInstanceTransform(owned.uniqueId, p.position, p.rotation, p.scale);
				if (instance.collides != p.collides)
				{
					foliage.SetInstanceCollides(owned.uniqueId, p.collides);
				}
				return true;
			}

			foliage.RemoveInstance(owned.uniqueId);
			instance.meshName = p.asset;
			instance.position = p.position;
			instance.rotation = p.rotation;
			instance.scale = p.scale;
			instance.pageIndex = newPage;
			instance.collides = p.collides;
			return foliage.AddInstance(instance);
		}
	}

	OasisBuildReport RebuildOasis(IWorldEditor& editor, pcg::OasisRecord& record, const pcg::OasisAssets& assets)
	{
		const Clock::time_point start = Clock::now();
		OasisBuildReport report;

		terrain::Terrain* terrain = editor.GetTerrain();
		if (!terrain || !editor.HasTerrain())
		{
			report.error = "The world has no terrain";
			return report;
		}

		// 1. Validate first. Validation does not depend on the heights, so any height function will
		//    do; a broken loop must leave the world untouched.
		pcg::OasisShape probe;
		const pcg::OasisError validation = pcg::PrepareOasis(record.loop, record.seed, record.params,
			[](float, float) { return 0.0f; }, probe);
		if (validation != pcg::OasisError::None)
		{
			report.error = String("Invalid oasis loop: ") + DescribeError(validation);
			report.milliseconds = MillisecondsSince(start);
			return report;
		}

		const pcg::WorldRect newInfluence = pcg::GetInfluenceRect(probe);
		if (!Contains(GetTerrainRect(*terrain), newInfluence))
		{
			report.error = "The oasis does not fit inside the terrain";
			report.milliseconds = MillisecondsSince(start);
			return report;
		}

		// 2. Load everything the old build touched and the new one will touch.
		pcg::WorldRect loadRect = newInfluence;
		pcg::WorldRect oldRect;
		if (GetOldContentRect(record, oldRect))
		{
			bool any = true;
			Include(loadRect, any, oldRect);
		}
		if (!LoadPagesOrFail(editor, loadRect, report))
		{
			report.milliseconds = MillisecondsSince(start);
			return report;
		}

		// The terrain brushes turn ocean placeholder pages into real terrain before writing; do the
		// same so no write of the contribution is silently dropped.
		terrain->MaterializeOceanPages(newInfluence.minX, newInfluence.minZ, newInfluence.maxX, newInfluence.maxZ);

		EditorTerrainAccess access(*terrain);
		access.SetDefaultWaterMaterial(assets.waterMaterial);

		// 3. Undo the previous contribution (keeps hand sculpting done since as a height delta).
		pcg::Unapply(access, record.terrain);

		// 4. Prepare on the current ground and write the new contribution.
		auto groundHeight = [terrain](const float x, const float z)
		{
			return terrain->GetSmoothHeightAt(x, z);
		};

		pcg::OasisShape shape;
		const pcg::OasisError prepared = pcg::PrepareOasis(record.loop, record.seed, record.params, groundHeight, shape);
		if (prepared != pcg::OasisError::None)
		{
			// Cannot happen (validation does not depend on heights), but never apply a bad shape.
			report.error = String("Invalid oasis loop: ") + DescribeError(prepared);
			ReportTouchedPages(access, report);
			report.milliseconds = MillisecondsSince(start);
			return report;
		}

		record.influence = pcg::GetInfluenceRect(shape);
		pcg::Apply(access, record.influence,
			[&shape](const float x, const float z, const float baseHeight) { return pcg::EvaluateTerrain(shape, x, z, baseHeight); },
			assets.terrainMaterial, record.terrain);
		report.terrainSamples = CountSamples(record.terrain);

		// 5. Placements on the final ground, reconciled against what the oasis owns.
		auto groundSlope = [terrain](const float x, const float z)
		{
			const Vector3 normal = terrain->GetSmoothNormalAt(x, z);
			return std::acos(std::clamp(normal.y, -1.0f, 1.0f)) * RadToDeg;
		};

		std::vector<pcg::Placement> placements;
		pcg::BuildPlacements(shape, assets, groundHeight, groundSlope, placements);

		InstancedFoliage& foliage = editor.GetInstancedFoliage();
		const pcg::ReconcileResult reconciled = pcg::Reconcile(record.placements, placements,
			[&editor, &foliage](const uint64 uniqueId, const pcg::PlacementKind kind)
			{
				return kind == pcg::PlacementKind::Entity ? editor.HasMapEntity(uniqueId) : FoliageExists(foliage, uniqueId);
			});

		std::map<uint64, pcg::PlacementKind> ownedKinds;
		for (const auto& owned : record.placements)
		{
			ownedKinds[owned.uniqueId] = owned.kind;
		}

		for (const uint64 uniqueId : reconciled.remove)
		{
			const auto it = ownedKinds.find(uniqueId);
			if (it != ownedKinds.end() && RemoveOwned(editor, uniqueId, it->second))
			{
				++report.removed;
			}
		}

		std::vector<pcg::OwnedPlacement> owned = reconciled.keep;
		report.kept = static_cast<uint32>(reconciled.keep.size());

		for (const auto& update : reconciled.update)
		{
			if (UpdatePlacement(editor, update))
			{
				owned.push_back(update);
				++report.updated;
				continue;
			}

			// Vanished since the existence check: create it again.
			if (const uint64 uniqueId = CreatePlacement(editor, record.id, update.written))
			{
				pcg::OwnedPlacement recreated = update;
				recreated.uniqueId = uniqueId;
				owned.push_back(recreated);
				++report.created;
			}
		}

		for (const auto& placement : reconciled.create)
		{
			const uint64 uniqueId = CreatePlacement(editor, record.id, placement);
			if (uniqueId == 0)
			{
				// Not owned: the next rebuild tries again.
				continue;
			}

			pcg::OwnedPlacement created;
			created.key = placement.key;
			created.uniqueId = uniqueId;
			created.kind = placement.kind;
			created.written = placement;
			owned.push_back(created);
			++report.created;
		}

		std::sort(owned.begin(), owned.end(), [](const pcg::OwnedPlacement& a, const pcg::OwnedPlacement& b)
		{
			return a.key < b.key;
		});
		record.placements = std::move(owned);

		foliage.RebuildDirtyCells();

		ReportTouchedPages(access, report);
		report.ok = true;
		report.milliseconds = MillisecondsSince(start);
		return report;
	}

	OasisBuildReport DeleteOasisContent(IWorldEditor& editor, pcg::OasisRecord& record)
	{
		const Clock::time_point start = Clock::now();
		OasisBuildReport report;

		pcg::WorldRect contentRect;
		if (!GetOldContentRect(record, contentRect))
		{
			// Nothing in the world.
			record.terrain.Clear();
			record.placements.clear();
			record.influence = pcg::WorldRect{};
			report.ok = true;
			report.milliseconds = MillisecondsSince(start);
			return report;
		}

		terrain::Terrain* terrain = editor.GetTerrain();
		if (!record.terrain.IsEmpty() && (!terrain || !editor.HasTerrain()))
		{
			report.error = "The world has no terrain";
			report.milliseconds = MillisecondsSince(start);
			return report;
		}

		// Without a terrain there are no pages to load (and no contribution, checked above).
		if (terrain && !LoadPagesOrFail(editor, contentRect, report))
		{
			report.milliseconds = MillisecondsSince(start);
			return report;
		}

		if (terrain)
		{
			EditorTerrainAccess access(*terrain);
			pcg::Unapply(access, record.terrain);
			ReportTouchedPages(access, report);
		}
		record.terrain.Clear();

		for (const auto& owned : record.placements)
		{
			if (RemoveOwned(editor, owned.uniqueId, owned.kind))
			{
				++report.removed;
			}
		}
		record.placements.clear();
		record.influence = pcg::WorldRect{};

		editor.GetInstancedFoliage().RebuildDirtyCells();

		report.ok = true;
		report.milliseconds = MillisecondsSince(start);
		return report;
	}
}
