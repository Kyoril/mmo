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
#include "terrain/page.h"
#include "terrain/terrain.h"

#include <algorithm>
#include <array>
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

		/// The rects MaterializeOceanPagesForWrites turns into terrain where they are open ocean: the influence when there
		/// are no write rects, else every non-empty write rect grown by the seam margin and clipped to the influence.
		std::vector<pcg::WorldRect> GetMaterializeRects(const pcg::WorldRect& influence, const std::vector<pcg::WorldRect>& writeRects)
		{
			std::vector<pcg::WorldRect> rects;
			if (writeRects.empty())
			{
				rects.push_back(influence);
			}
			for (const pcg::WorldRect& rect : writeRects)
			{
				if (rect.minX > rect.maxX || rect.minZ > rect.maxZ)
				{
					// Empty rect: nothing to write there.
					continue;
				}
				rects.push_back({ std::max(rect.minX - WriteRectSeamMargin, influence.minX), std::max(rect.minZ - WriteRectSeamMargin, influence.minZ),
					std::min(rect.maxX + WriteRectSeamMargin, influence.maxX), std::min(rect.maxZ + WriteRectSeamMargin, influence.maxZ) });
			}
			return rects;
		}

		/// Puts the placements into the world, reconciled against what the generator owns (remove / update / create),
		/// and stores the new owned placements. Counts go to the report.
		void ReconcilePlacements(IWorldEditor& editor, std::vector<pcg::Placement> placements, pcg::OwnedContent& owned,
			const uint64 ownerId, const String& ownerLabel, OasisBuildReport& report)
		{
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
		}
	}

	pcg::WorldRect GetTerrainRect(const terrain::Terrain& terrain)
	{
		const auto halfX = static_cast<float>(static_cast<double>(terrain.GetWidth()) / 2.0 * terrain::constants::PageSize);
		const auto halfZ = static_cast<float>(static_cast<double>(terrain.GetHeight()) / 2.0 * terrain::constants::PageSize);
		return pcg::WorldRect{ -halfX, -halfZ, halfX, halfZ };
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
		if (writesTerrain)
		{
			MaterializeOceanPagesForWrites(*terrain, content.influence, content.writeRects);
		}

		EditorTerrainAccess access(*terrain);
		access.SetDefaultWaterMaterial(content.waterMaterial);

		// The previous contribution and the terrain values under it before the undo.
		pcg::TerrainContribution recorded;
		pcg::TerrainContribution captured;

		// 2. Undo the previous contribution (keeps hand sculpting done since as a height delta).
		if (writesTerrain)
		{
			if (!CheckRecordMatchesTerrain(access, owned.terrain, report.error))
			{
				report.milliseconds = MillisecondsSince(start);
				return report;
			}
			// A failing prepare puts these samples back, so the old contribution and the record stay.
			recorded = owned.terrain;
			captured = pcg::CaptureSamples(access, owned.terrain);
			pcg::Unapply(access, owned.terrain);

			// The contribution is gone from the world now; the record must not claim it while it is gone
			// (Unapply clears it already, this keeps the invariant explicit).
			owned.terrain.Clear();
		}

		// 3. Let the generator prepare on the current ground, then write the new contribution.
		const GroundFunction groundHeight = [terrain](const float x, const float z)
		{
			return terrain->GetSmoothHeightAt(x, z);
		};

		if (content.prepare && !content.prepare(groundHeight, report.error))
		{
			// Put the old contribution back exactly as it was (hand sculpting since included): the
			// world and the record end as before the call.
			if (writesTerrain)
			{
				pcg::RestoreSamples(access, captured);
				owned.terrain = std::move(recorded);
			}
			ReportTouchedPages(access, report);
			report.milliseconds = MillisecondsSince(start);
			return report;
		}

		if (writesTerrain)
		{
			owned.influence = content.influence;
			std::vector<pcg::ChannelTerrainTargetFunction> targets;
			if (content.terrainWorkers)
			{
				targets = content.terrainWorkers();
			}
			if (targets.empty())
			{
				targets.push_back(content.terrain);
			}
			pcg::Apply(access, owned.influence, targets, content.tileMaterial, owned.terrain);
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
		ReconcilePlacements(editor, std::move(placements), owned, ownerId, ownerLabel, report);

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

	bool NeedsOceanMaterialization(const terrain::Terrain& terrain, const pcg::WorldRect& influence, const std::vector<pcg::WorldRect>& writeRects)
	{
		// The pages Terrain::MaterializeOceanPages visits for each rect.
		const double halfWidth = static_cast<double>(terrain.GetWidth()) * terrain::constants::PageSize * 0.5;
		const double halfHeight = static_cast<double>(terrain.GetHeight()) * terrain::constants::PageSize * 0.5;
		const auto pageIndex = [](const float value, const double half)
		{
			return static_cast<int32>(std::floor((static_cast<double>(value) + half) / terrain::constants::PageSize));
		};
		for (const pcg::WorldRect& rect : GetMaterializeRects(influence, writeRects))
		{
			const int32 minX = std::max(pageIndex(rect.minX, halfWidth), 0);
			const int32 minZ = std::max(pageIndex(rect.minZ, halfHeight), 0);
			const int32 maxX = std::min(pageIndex(rect.maxX, halfWidth), static_cast<int32>(terrain.GetWidth()) - 1);
			const int32 maxZ = std::min(pageIndex(rect.maxZ, halfHeight), static_cast<int32>(terrain.GetHeight()) - 1);
			for (int32 pageZ = minZ; pageZ <= maxZ; ++pageZ)
			{
				for (int32 pageX = minX; pageX <= maxX; ++pageX)
				{
					const terrain::Page* page = terrain.GetPage(static_cast<uint32>(pageX), static_cast<uint32>(pageZ));
					if (page && page->IsOceanPlaceholder())
					{
						return true;
					}
				}
			}
		}
		return false;
	}

	void MaterializeOceanPagesForWrites(const terrain::Terrain& terrain, const pcg::WorldRect& influence, const std::vector<pcg::WorldRect>& writeRects)
	{
		for (const pcg::WorldRect& rect : GetMaterializeRects(influence, writeRects))
		{
			terrain.MaterializeOceanPages(rect.minX, rect.minZ, rect.maxX, rect.maxZ);
		}
	}

	namespace
	{
		/// Loads the session's pages again (see BeginContentRebuild).
		bool ReloadSessionPages(const ContentRebuildSession& session)
		{
			OasisBuildReport loadReport;
			return session.editor && LoadPagesOrFail(*session.editor, session.loadRect, loadReport);
		}
	}

	ContentRebuildSession::ContentRebuildSession() = default;

	ContentRebuildSession::~ContentRebuildSession() = default;

	bool BeginContentRebuild(IWorldEditor& editor, const pcg::WorldRect& influence, pcg::OwnedContent& owned, ContentRebuildSession& session,
		String& error)
	{
		session.access.reset();
		session.recorded.Clear();
		session.captured.Clear();
		session.active = false;

		terrain::Terrain* terrain = editor.GetTerrain();
		if (!terrain || !editor.HasTerrain())
		{
			error = "The world has no terrain";
			return false;
		}
		if (!Contains(GetTerrainRect(*terrain), influence))
		{
			error = "The influence does not fit inside the terrain";
			return false;
		}

		// Everything the old build touched and the new one will touch.
		pcg::WorldRect loadRect = influence;
		pcg::WorldRect oldRect;
		if (GetOldContentRect(owned, oldRect))
		{
			bool any = true;
			Include(loadRect, any, oldRect);
		}
		OasisBuildReport loadReport;
		if (!LoadPagesOrFail(editor, loadRect, loadReport))
		{
			error = loadReport.error;
			return false;
		}

		session.access = std::make_unique<EditorTerrainAccess>(*terrain);
		session.access->SetDeferGpuUpdates(true);
		if (!CheckRecordMatchesTerrain(*session.access, owned.terrain, error))
		{
			session.access.reset();
			return false;
		}

		// The old contribution leaves the terrain data (normals included); the renderer keeps showing it until the session
		// ends. Unapply clears owned.terrain: the record must not claim a contribution that is not on the terrain.
		session.editor = &editor;
		session.loadRect = loadRect;
		session.recorded = owned.terrain;
		session.captured = pcg::CaptureSamples(*session.access, owned.terrain);
		pcg::Unapply(*session.access, owned.terrain);
		owned.terrain.Clear();
		session.influence = influence;
		session.active = true;
		return true;
	}

	void AbortContentRebuild(ContentRebuildSession& session, pcg::OwnedContent& owned)
	{
		if (!session.active)
		{
			return;
		}

		// The samples the old contribution recorded get the values they had before the undo, hand sculpting included.
		// Streaming may have unloaded tiles since the start; tile materials need them.
		ReloadSessionPages(session);
		pcg::RestoreSamples(*session.access, session.captured);
		owned.terrain = std::move(session.recorded);
		session.access->FlushGpuUpdates();
		session.active = false;
	}

	OasisBuildReport FinishContentRebuild(IWorldEditor& editor, ContentRebuildSession& session, const pcg::TerrainSamples& samples,
		const String& tileMaterial, const String& waterMaterial,
		const std::function<void(const GroundFunction& ground, const GroundFunction& slope, std::vector<pcg::Placement>& out)>& placements,
		pcg::OwnedContent& owned, const uint64 ownerId, const String& ownerLabel)
	{
		const Clock::time_point start = Clock::now();
		OasisBuildReport report;
		terrain::Terrain* terrain = editor.GetTerrain();
		if (!session.active || !terrain)
		{
			report.error = "No rebuild in progress";
			report.milliseconds = MillisecondsSince(start);
			return report;
		}

		// Streaming may have unloaded tiles since the start; tile material writes need them.
		if (!ReloadSessionPages(session))
		{
			AbortContentRebuild(session, owned);
			report.error = "The terrain pages of the build could not be loaded again; the world was not changed.";
			report.milliseconds = MillisecondsSince(start);
			return report;
		}

		EditorTerrainAccess& access = *session.access;
		access.SetDefaultWaterMaterial(waterMaterial);
		pcg::TerrainContribution written;
		if (!pcg::WriteTerrainSamples(access, samples, tileMaterial, written))
		{
			AbortContentRebuild(session, owned);
			report.error = "The terrain changed while the build was computed; the world was not changed. Rebuild again.";
			report.milliseconds = MillisecondsSince(start);
			return report;
		}
		owned.terrain = std::move(written);
		owned.influence = session.influence;
		report.terrainSamples = CountSamples(owned.terrain);
		access.FlushGpuUpdates();
		session.active = false;

		// Placements on the final ground, reconciled against what the generator owns.
		const GroundFunction groundHeight = [terrain](const float x, const float z)
		{
			return terrain->GetSmoothHeightAt(x, z);
		};
		const GroundFunction groundSlope = [terrain](const float x, const float z)
		{
			const Vector3 normal = terrain->GetSmoothNormalAt(x, z);
			return std::acos(std::clamp(normal.y, -1.0f, 1.0f)) * RadToDeg;
		};
		std::vector<pcg::Placement> next;
		if (placements)
		{
			placements(groundHeight, groundSlope, next);
		}
		ReconcilePlacements(editor, std::move(next), owned, ownerId, ownerLabel, report);

		ReportTouchedPages(access, report);
		report.ok = true;
		report.milliseconds = MillisecondsSince(start);
		return report;
	}
}
