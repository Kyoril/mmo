// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "pcg/owned_content.h"
#include "pcg/placement.h"
#include "pcg/terrain_contribution.h"

#include <functional>
#include <memory>
#include <vector>

namespace mmo
{
	class EditorTerrainAccess;
	class IWorldEditor;

	namespace terrain
	{
		class Terrain;
	}

	/// @brief World extent (x/z) of a terrain: its pages centred on the origin.
	pcg::WorldRect GetTerrainRect(const terrain::Terrain& terrain);

	/// @brief Outcome of applying or removing the content of a generator.
	struct OasisBuildReport
	{
		/// @brief False if the operation was refused or failed; error says why.
		bool ok = false;
		/// @brief Human readable reason when ok is false.
		String error;
		/// @brief Wall clock time the operation took.
		double milliseconds = 0.0;
		/// @brief Terrain samples (heights, splat pixels, water quads/heights, tile materials) the
		///        new contribution wrote.
		uint32 terrainSamples = 0;
		/// @brief Owned placements left as they were.
		uint32 kept = 0;
		/// @brief Owned placements moved to a new transform (same object).
		uint32 updated = 0;
		/// @brief Owned objects deleted.
		uint32 removed = 0;
		/// @brief Objects created.
		uint32 created = 0;
		/// @brief Terrain pages (x << 8 | z, the world page index) whose terrain data changed. Their
		///        far-terrain bake (.tlod) is stale: the world save does not re-bake it, run
		///        mmo_edit --bake-terrain-lod afterwards.
		std::vector<uint16> touchedPages;
		/// @brief Non-fatal notes of a successful build (e.g. paint layers a generator had to skip
		///        on some tiles). Empty for most builds.
		std::vector<String> warnings;
	};

	/// @brief Report of any PCG generator build (same type as the oasis report).
	using GeneratorBuildReport = OasisBuildReport;

	/// @brief Height or slope (degrees) of the current ground at a world x/z position.
	using GroundFunction = std::function<float(float x, float z)>;

	/// @brief Everything one generator wants in the world for one build.
	struct GeneratedContent
	{
		/// @brief Terrain influence of this build. Must lie inside the terrain. Every page it touches
		///        is loaded before anything is changed.
		pcg::WorldRect influence{};

		/// @brief Optional rects (inside the influence) covering every terrain sample the build may write.
		///        Ocean placeholder pages are turned into real terrain before writing, but only where they
		///        overlap one of these rects (grown by 1 m for seam samples), so a long section path does
		///        not materialise every open-ocean page of its bounding box. Empty = the whole influence. A rect
		///        with min > max is empty: a list of only such rects materialises nothing (a build that writes
		///        no terrain but must still undo its previous contribution).
		std::vector<pcg::WorldRect> writeRects;

		/// @brief Optional step run after the previous contribution was undone and before the new
		///        one is written, with the height of the ground as it is now. Generators whose shape
		///        depends on the ground (the oasis rim) prepare it here; terrain and placements may
		///        capture what it prepares. Returning false aborts the build with the given error; the
		///        previous contribution is then put back exactly (the terrain samples it recorded get the
		///        values they had before the undo, the record keeps it) and the placements are not touched,
		///        so the world ends as before (ocean pages materialised for writeRects stay materialised).
		///        May be empty.
		std::function<bool(const GroundFunction& ground, String& error)> prepare;

		/// @brief What the generator wants on the terrain inside the influence. May be empty: the build
		///        then leaves the terrain alone (no ocean pages materialised, the recorded contribution is
		///        neither undone nor rewritten) and only reconciles the placements. A set function that
		///        returns weight 0 everywhere undoes the previous contribution and writes nothing. Each write pass
		///        names the channel it reads (see pcg::Apply), so a target may skip work only other channels need.
		pcg::ChannelTerrainTargetFunction terrain;

		/// @brief Optional: called right before the terrain is written (after prepare, with the pages materialised) to
		///        make one target per worker thread for pcg::Apply. Each must return what `terrain` returns; the first
		///        runs on the calling thread and also writes water and tile materials. Empty, or returning none: `terrain`
		///        alone on the calling thread.
		std::function<std::vector<pcg::ChannelTerrainTargetFunction>()> terrainWorkers;

		/// @brief Material set on tiles where a target asks for it ("" = none).
		String tileMaterial;

		/// @brief Water material given to a page that receives water but has none yet ("" = none).
		String waterMaterial;

		/// @brief Produces the placements on the final ground (after the terrain was written), given
		///        the ground height and slope (degrees). May be empty (no placements).
		std::function<void(const GroundFunction& ground, const GroundFunction& slope, std::vector<pcg::Placement>& out)> placements;
	};

	/// @brief Puts a generator's content into the world, replacing what it put there before.
	///        The content must have been validated by the caller. Loads the pages of the old and new
	///        influence and of every owned placement and refuses (the world stays untouched) if a page
	///        is not ready or the recorded contribution no longer matches the terrain. Then undoes the
	///        previous contribution, runs content.prepare (a failing prepare puts the previous contribution
	///        back and ends the call), writes the new contribution, builds the
	///        placements and reconciles them against the owned ones (remove / update / create).
	///        Material overrides of every placement are sorted by index before they are compared or
	///        created. Afterwards owned holds the new influence, contribution and placements.
	/// @param editor The world editor to work on. Must have a terrain.
	/// @param content What the generator wants.
	/// @param owned What the generator owns; updated in place.
	/// @param ownerId Id of the generator instance; part of the names of created map entities.
	/// @param ownerLabel Generator kind shown in the names of created map entities ("Oasis").
	/// @return The report.
	OasisBuildReport ApplyGeneratedContent(IWorldEditor& editor, const GeneratedContent& content, pcg::OwnedContent& owned,
		uint64 ownerId, const String& ownerLabel);

	/// @brief Removes everything a generator put into the world: its terrain contribution is undone
	///        and all owned entities and foliage instances are deleted. Refused (nothing changes) if a
	///        page cannot be loaded or the contribution no longer matches the terrain. Leaves owned
	///        empty on success.
	/// @param editor The world editor to work on.
	/// @param owned What the generator owns; cleared on success.
	/// @return The report (removed = number of owned objects deleted).
	OasisBuildReport RemoveGeneratedContent(IWorldEditor& editor, pcg::OwnedContent& owned);

	/// @brief Whether writing over the given rects would first turn open-ocean placeholder pages into terrain (see
	///        MaterializeOceanPagesForWrites): such a write changes the base terrain under it.
	/// @param terrain The terrain.
	/// @param influence The build's influence.
	/// @param writeRects The rects the build may write (GeneratedContent::writeRects rules).
	bool NeedsOceanMaterialization(const terrain::Terrain& terrain, const pcg::WorldRect& influence, const std::vector<pcg::WorldRect>& writeRects);

	/// @brief Turns the open-ocean placeholder pages under the write rects (grown by a seam margin, clipped to the
	///        influence; the whole influence when there are none, nothing for empty rects) into real terrain, as the
	///        terrain brushes do before they write.
	void MaterializeOceanPagesForWrites(const terrain::Terrain& terrain, const pcg::WorldRect& influence, const std::vector<pcg::WorldRect>& writeRects);

	/// @brief A generator rebuild taken apart, so that the new contribution can be computed in between on another
	///        thread (graph instance rebuilds): BeginContentRebuild takes the previous terrain contribution out of the
	///        terrain data, the caller computes the new contribution on that base terrain, then FinishContentRebuild writes
	///        it and reconciles the placements, or AbortContentRebuild puts the old contribution back. Until then the
	///        renderer keeps showing the previous build (the GPU updates are deferred), and nothing else may write the
	///        terrain. Main thread only.
	struct ContentRebuildSession
	{
		ContentRebuildSession();
		~ContentRebuildSession();

		/// @brief Terrain access of the session, with deferred GPU updates.
		std::unique_ptr<EditorTerrainAccess> access;

		/// @brief The previous contribution (put back by AbortContentRebuild).
		pcg::TerrainContribution recorded;

		/// @brief The terrain values under it before the undo (RestoreSamples).
		pcg::TerrainContribution captured;

		/// @brief The influence of the new build.
		pcg::WorldRect influence{};

		/// @brief The editor and the rect whose pages Begin loaded (loaded again before anything is written).
		IWorldEditor* editor = nullptr;
		pcg::WorldRect loadRect{};

		/// @brief True between a successful BeginContentRebuild and Finish or Abort.
		bool active = false;
	};

	/// @brief Starts a rebuild: loads the pages of the influence and of everything the generator owns, refuses (the world
	///        stays untouched) if one is not ready, the influence leaves the terrain or the recorded contribution does not
	///        match the terrain any more, then undoes the previous contribution in the terrain data and clears
	///        owned.terrain.
	/// @return False with the reason in error if refused.
	bool BeginContentRebuild(IWorldEditor& editor, const pcg::WorldRect& influence, pcg::OwnedContent& owned, ContentRebuildSession& session,
		String& error);

	/// @brief Ends a session without a new build: the old contribution is put back exactly (hand sculpting since included)
	///        and owned.terrain holds it again. Does nothing for a session that is not active.
	void AbortContentRebuild(ContentRebuildSession& session, pcg::OwnedContent& owned);

	/// @brief Ends a session with the new build: writes the samples (pcg::WriteTerrainSamples; a page that gets water and
	///        has no water material gets waterMaterial), updates the renderer, builds the placements on the final ground and
	///        reconciles them like ApplyGeneratedContent. Afterwards owned holds the session's influence, the new
	///        contribution and placements. If the samples were computed on another base than the terrain holds, the session
	///        is aborted instead and the report says so.
	/// @param placements Produces the placements on the final ground; may be empty (no placements).
	OasisBuildReport FinishContentRebuild(IWorldEditor& editor, ContentRebuildSession& session, const pcg::TerrainSamples& samples,
		const String& tileMaterial, const String& waterMaterial,
		const std::function<void(const GroundFunction& ground, const GroundFunction& slope, std::vector<pcg::Placement>& out)>& placements,
		pcg::OwnedContent& owned, uint64 ownerId, const String& ownerLabel);
}
