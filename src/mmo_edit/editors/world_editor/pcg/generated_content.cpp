// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "generated_content.h"

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

		/// Margin around GeneratedContent::writeRects when materialising ocean pages: a seam sample is
		/// written into both pages sharing it.
		constexpr float WriteRectSeamMargin = 1.0f;

		/// Category of every map entity a generator creates.
		const String PcgCategory = "Pcg";

		using Clock = std::chrono::steady_clock;

		double MillisecondsSince(const Clock::time_point start)
		{
			return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
		}

		/// "<label> <owner id hex> <placement key hex>", both as 16 lower case hex digits.
		String MakeEntityName(const String& ownerLabel, const uint64 ownerId, const uint64 key)
		{
			char buffer[64];
			std::snprintf(buffer, sizeof(buffer), " %016llx %016llx",
				static_cast<unsigned long long>(ownerId), static_cast<unsigned long long>(key));
			return ownerLabel + buffer;
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

		/// The old influence plus every owned object's position: everything that has to be loaded
		/// to undo the previous build.
		bool GetOldContentRect(const pcg::OwnedContent& owned, pcg::WorldRect& out)
		{
			bool any = false;
			if (!owned.terrain.IsEmpty())
			{
				Include(out, any, owned.influence);
			}

			for (const auto& placement : owned.placements)
			{
				const Vector3& p = placement.written.position;
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

		/// Refuses (fills the error) when the recorded contribution is not on the terrain any more:
		/// Unapply would then shift the heights by minus the recorded delta.
		bool CheckRecordMatchesTerrain(const EditorTerrainAccess& access, const pcg::TerrainContribution& contribution, String& error)
		{
			size_t matching = 0;
			size_t total = 0;
			const float ratio = pcg::ContributionMatchRatio(access, contribution, &matching, &total);
			if (total == 0 || ratio >= pcg::kMinContributionMatchRatio)
			{
				return true;
			}

			error = "The PCG record does not match the terrain (" + std::to_string(matching) + " of " + std::to_string(total)
				+ " samples match); the world was not changed.";
			ELOG("PCG: " << error);
			return false;
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
			if (kind != pcg::PlacementKind::Foliage)
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
		uint64 CreatePlacement(IWorldEditor& editor, const String& ownerLabel, const uint64 ownerId, const pcg::Placement& placement)
		{
			if (placement.kind != pcg::PlacementKind::Foliage)
			{
				const uint64 uniqueId = editor.GenerateEntityUniqueId();
				if (!editor.CreatePlacementEntity(placement, uniqueId) || !editor.HasMapEntity(uniqueId))
				{
					WLOG("PCG: failed to create entity " << placement.asset);
					return 0;
				}

				editor.SetMapEntityInfo(uniqueId, MakeEntityName(ownerLabel, ownerId, placement.key), PcgCategory);
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
			if (owned.kind != pcg::PlacementKind::Foliage)
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

		/// Material overrides are compared and stored sorted by material index.
		void SortMaterialOverrides(std::vector<pcg::Placement>& placements)
		{
			for (pcg::Placement& placement : placements)
			{
				std::stable_sort(placement.materialOverrides.begin(), placement.materialOverrides.end(),
					[](const pcg::PlacementMaterialOverride& a, const pcg::PlacementMaterialOverride& b)
					{
						return a.materialIndex < b.materialIndex;
					});
			}
		}
	}

	OasisBuildReport ApplyGeneratedContent(IWorldEditor& editor, const GeneratedContent& content, pcg::OwnedContent& owned,
		const uint64 ownerId, const String& ownerLabel)
	{
		const Clock::time_point start = Clock::now();
		OasisBuildReport report;

		terrain::Terrain* terrain = editor.GetTerrain();
		if (!terrain || !editor.HasTerrain())
		{
			report.error = "The world has no terrain";
			report.milliseconds = MillisecondsSince(start);
			return report;
		}

		if (!Contains(GetTerrainRect(*terrain), content.influence))
		{
			report.error = "The " + ownerLabel + " does not fit inside the terrain";
			report.milliseconds = MillisecondsSince(start);
			return report;
		}

		// 1. Load everything the old build touched and the new one will touch.
		pcg::WorldRect loadRect = content.influence;
		pcg::WorldRect oldRect;
		if (GetOldContentRect(owned, oldRect))
		{
			bool any = true;
			Include(loadRect, any, oldRect);
		}
		if (!LoadPagesOrFail(editor, loadRect, report))
		{
			report.milliseconds = MillisecondsSince(start);
			return report;
		}

		// Without a terrain target the terrain is not touched at all: a recorded contribution (if any)
		// stays as it is, and only the placements are reconciled.
		const bool writesTerrain = static_cast<bool>(content.terrain);

		// The terrain brushes turn ocean placeholder pages into real terrain before writing; do the
		// same so no write of the contribution is silently dropped. Only where the build may write:
		// a page that stays open ocean is neither changed nor saved.
		if (!writesTerrain)
		{
			// Nothing is written.
		}
		else if (content.writeRects.empty())
		{
			terrain->MaterializeOceanPages(content.influence.minX, content.influence.minZ, content.influence.maxX, content.influence.maxZ);
		}
		else
		{
			for (const pcg::WorldRect& rect : content.writeRects)
			{
				terrain->MaterializeOceanPages(
					std::max(rect.minX - WriteRectSeamMargin, content.influence.minX),
					std::max(rect.minZ - WriteRectSeamMargin, content.influence.minZ),
					std::min(rect.maxX + WriteRectSeamMargin, content.influence.maxX),
					std::min(rect.maxZ + WriteRectSeamMargin, content.influence.maxZ));
			}
		}

		EditorTerrainAccess access(*terrain);
		access.SetDefaultWaterMaterial(content.waterMaterial);

		// 2. Undo the previous contribution (keeps hand sculpting done since as a height delta).
		if (writesTerrain)
		{
			if (!CheckRecordMatchesTerrain(access, owned.terrain, report.error))
			{
				report.milliseconds = MillisecondsSince(start);
				return report;
			}
			pcg::Unapply(access, owned.terrain);

			// The contribution is gone from the world now; the record must not claim it even if prepare
			// fails below (Unapply clears it already, this keeps the invariant explicit).
			owned.terrain.Clear();
		}

		// 3. Let the generator prepare on the current ground, then write the new contribution.
		const GroundFunction groundHeight = [terrain](const float x, const float z)
		{
			return terrain->GetSmoothHeightAt(x, z);
		};

		if (content.prepare && !content.prepare(groundHeight, report.error))
		{
			ReportTouchedPages(access, report);
			report.milliseconds = MillisecondsSince(start);
			return report;
		}

		if (writesTerrain)
		{
			owned.influence = content.influence;
			pcg::Apply(access, owned.influence, content.terrain, content.tileMaterial, owned.terrain);
			report.terrainSamples = CountSamples(owned.terrain);
		}
		else if (owned.terrain.IsEmpty())
		{
			// A kept contribution keeps the influence it was written with (its undo loads those pages).
			owned.influence = content.influence;
		}

		// 4. Placements on the final ground, reconciled against what the generator owns.
		const GroundFunction groundSlope = [terrain](const float x, const float z)
		{
			const Vector3 normal = terrain->GetSmoothNormalAt(x, z);
			return std::acos(std::clamp(normal.y, -1.0f, 1.0f)) * RadToDeg;
		};

		std::vector<pcg::Placement> placements;
		if (content.placements)
		{
			content.placements(groundHeight, groundSlope, placements);
		}
		SortMaterialOverrides(placements);

		InstancedFoliage& foliage = editor.GetInstancedFoliage();
		const pcg::ReconcileResult reconciled = pcg::Reconcile(owned.placements, placements,
			[&editor, &foliage](const uint64 uniqueId, const pcg::PlacementKind kind)
			{
				return kind != pcg::PlacementKind::Foliage ? editor.HasMapEntity(uniqueId) : FoliageExists(foliage, uniqueId);
			});

		std::map<uint64, pcg::PlacementKind> ownedKinds;
		for (const auto& placement : owned.placements)
		{
			ownedKinds[placement.uniqueId] = placement.kind;
		}

		for (const uint64 uniqueId : reconciled.remove)
		{
			const auto it = ownedKinds.find(uniqueId);
			if (it != ownedKinds.end() && RemoveOwned(editor, uniqueId, it->second))
			{
				++report.removed;
			}
		}

		std::vector<pcg::OwnedPlacement> nextOwned = reconciled.keep;
		report.kept = static_cast<uint32>(reconciled.keep.size());

		for (const auto& update : reconciled.update)
		{
			if (UpdatePlacement(editor, update))
			{
				nextOwned.push_back(update);
				++report.updated;
				continue;
			}

			// Vanished since the existence check: create it again.
			if (const uint64 uniqueId = CreatePlacement(editor, ownerLabel, ownerId, update.written))
			{
				pcg::OwnedPlacement recreated = update;
				recreated.uniqueId = uniqueId;
				nextOwned.push_back(recreated);
				++report.created;
			}
		}

		for (const auto& placement : reconciled.create)
		{
			const uint64 uniqueId = CreatePlacement(editor, ownerLabel, ownerId, placement);
			if (uniqueId == 0)
			{
				// Not owned: the next build tries again.
				continue;
			}

			pcg::OwnedPlacement created;
			created.key = placement.key;
			created.uniqueId = uniqueId;
			created.kind = placement.kind;
			created.written = placement;
			nextOwned.push_back(created);
			++report.created;
		}

		std::sort(nextOwned.begin(), nextOwned.end(), [](const pcg::OwnedPlacement& a, const pcg::OwnedPlacement& b)
		{
			return a.key < b.key;
		});
		owned.placements = std::move(nextOwned);

		foliage.RebuildDirtyCells();

		ReportTouchedPages(access, report);
		report.ok = true;
		report.milliseconds = MillisecondsSince(start);
		return report;
	}

	OasisBuildReport RemoveGeneratedContent(IWorldEditor& editor, pcg::OwnedContent& owned)
	{
		const Clock::time_point start = Clock::now();
		OasisBuildReport report;

		pcg::WorldRect contentRect;
		if (!GetOldContentRect(owned, contentRect))
		{
			// Nothing in the world.
			owned.terrain.Clear();
			owned.placements.clear();
			owned.influence = pcg::WorldRect{};
			report.ok = true;
			report.milliseconds = MillisecondsSince(start);
			return report;
		}

		terrain::Terrain* terrain = editor.GetTerrain();
		if (!owned.terrain.IsEmpty() && (!terrain || !editor.HasTerrain()))
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
			if (!CheckRecordMatchesTerrain(access, owned.terrain, report.error))
			{
				report.milliseconds = MillisecondsSince(start);
				return report;
			}
			pcg::Unapply(access, owned.terrain);
			ReportTouchedPages(access, report);
		}
		owned.terrain.Clear();

		for (const auto& placement : owned.placements)
		{
			if (RemoveOwned(editor, placement.uniqueId, placement.kind))
			{
				++report.removed;
			}
		}
		owned.placements.clear();
		owned.influence = pcg::WorldRect{};

		editor.GetInstancedFoliage().RebuildDirtyCells();

		report.ok = true;
		report.milliseconds = MillisecondsSince(start);
		return report;
	}
}
