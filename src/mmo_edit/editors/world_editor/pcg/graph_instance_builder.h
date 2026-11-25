// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base_terrain_sampler.h"
#include "editor_terrain_sampler.h"
#include "generated_content.h"

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "pcg/graph_instance_record.h"
#include "pcg/graph/controls.h"
#include "pcg/graph/evaluator.h"
#include "pcg/graph/graph.h"
#include "pcg/graph/graph_output.h"
#include "pcg/graph/node_util.h"
#include "pcg/graph/terrain_compose.h"
#include "pcg/section_asset.h"

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace mmo
{
	class IWorldEditor;
	class ProceduralEditMode;

	/// @brief File extension of graph assets.
	constexpr const char* GraphAssetExtension = ".hpcgg";

	/// @brief Name of the graph instance setting (ProceduralEditMode::CreateGraphInstance, --pcg-new-graph-instance --set)
	///        that sets the record's borderFade instead of a graph parameter.
	constexpr const char* GraphInstanceBorderFadeSetting = "borderFade";

	/// @brief A placed control that could not be resolved against its control type (missing or invalid type, or
	///        geometry that does not fit the type's shape).
	struct UnresolvedPlacedControl
	{
		/// @brief Id of the placed control.
		uint64 id = 0;

		/// @brief Name of the placed control.
		String name;

		/// @brief Why it could not be resolved.
		String reason;

		/// @brief Conservative x/z bounds of its geometry (see GetPlacedControlBounds); invalid when they cannot be
		///        computed.
		pcg::graph::Bounds2D bounds;
	};

	/// @brief Conservative x/z bounds of a placed control. With a type: its spline (loops and splines), its box
	///        footprint at any yaw (volumes) or its position (points). Without a type (missing or invalid) the shape is
	///        unknown: the spline's bounds if it has points, else the box footprint around the position.
	/// @param instance The placed control.
	/// @param type Its control type, or null if unknown.
	/// @return The bounds; invalid if a spline shape has no points.
	pcg::graph::Bounds2D GetPlacedControlBounds(const pcg::graph::ControlInstance& instance, const pcg::graph::ControlType* type);

	/// @brief Everything a graph instance's evaluation reads besides the graph: region (the instance's area), seed,
	///        parameter overrides, the resolved placed controls, the editor terrain sampler and the point limit.
	/// @details context points into controls and sampler, so the struct must stay where it was built (it is neither
	///          copyable nor movable). The resolved controls point into the procedural mode's controls and control
	///          library; they are valid until ProceduralEditMode::GetControlsRevision changes.
	struct GraphInstanceContext
	{
		GraphInstanceContext() = default;
		GraphInstanceContext(const GraphInstanceContext&) = delete;
		GraphInstanceContext& operator=(const GraphInstanceContext&) = delete;

		/// @brief The evaluation context. parameters holds the record's overrides (Evaluate merges them over the
		///        graph's defaults).
		pcg::graph::EvalContext context;

		/// @brief Storage of the resolved placed controls context.controls points at.
		std::vector<pcg::graph::ResolvedControl> controls;

		/// @brief Problems found while resolving the placed controls.
		std::vector<String> warnings;

		/// @brief The placed controls that could not be resolved (they are not in controls).
		std::vector<UnresolvedPlacedControl> unresolved;

		/// @brief Terrain sampler context.terrain points at (the live terrain).
		std::unique_ptr<EditorTerrainSampler> sampler;

		/// @brief Set by UseBaseTerrain: the base terrain over sampler, which context.terrain then points at.
		std::unique_ptr<BaseTerrainSampler> baseSampler;
	};

	/// @brief One evaluation of a graph instance, not applied to the world.
	struct GraphInstanceEvaluation
	{
		/// @brief The graph as read from its file.
		pcg::graph::Graph graph;

		/// @brief The evaluation result. When result.issues is non-empty nothing was executed.
		pcg::graph::EvalResult result;

		/// @brief The spawned points converted to placements and clipped to the area (empty if the graph did not
		///        validate).
		pcg::graph::CollectedPlacements collected;

		/// @brief Control resolution warnings, parameter merge warnings and the collection's warnings.
		std::vector<String> warnings;

		/// @brief The placed controls that could not be resolved (the evaluation ran without them).
		std::vector<UnresolvedPlacedControl> unresolved;
	};

	/// @brief Section assets read through LoadSectionAsset, each once, the way a graph rebuild reads them: a missing,
	///        unreadable or stale asset (its section world changed since the bake) is an error. The write times of the
	///        asset and of its section world are recorded when an asset is read, so a long-lived cache (the graph
	///        editor's debug view) can drop what changed on disk since (DropChanged). Main thread only.
	class SectionAssetCache final : public NonCopyable
	{
	public:
		/// @brief The asset, or null with the reason in error. The pointer stays valid until the entry is dropped
		///        (DropChanged, Clear) or the cache is destroyed.
		const pcg::SectionAsset* Get(const String& path, String& error);

		/// @brief A provider for pcg::graph::PreparedTerrain::Prepare reading through this cache, which must outlive it.
		[[nodiscard]] pcg::graph::SectionAssetProvider MakeProvider();

		/// @brief The asset if it was read already (null with the reason in error if it failed to read); never reads a
		///        file, so it may be called from another thread while nothing changes the cache.
		/// @param cached Receives false if the asset was not read yet (the result is null then).
		const pcg::SectionAsset* Find(const String& path, String& error, bool& cached) const;

		/// @brief Drops every entry whose asset file or section world file has another write time (or appeared or
		///        disappeared) since it was read. Stats two files per entry, so call it sparingly.
		/// @return True if an entry was dropped.
		bool DropChanged();

		/// @brief Drops every entry.
		void Clear();

		/// @brief Changes whenever an entry is dropped, so callers can key what they built from the assets on it.
		[[nodiscard]] uint64 GetRevision() const;

	private:
		struct Entry
		{
			std::unique_ptr<pcg::SectionAsset> asset;
			String error;
			std::optional<std::filesystem::file_time_type> assetTime;
			std::optional<std::filesystem::file_time_type> worldTime;
		};

		std::map<String, Entry> m_entries;
		uint64 m_revision = 0;
	};

	/// @brief Reads and parses a graph asset (.hpcgg).
	/// @param graphPath Asset path of the graph.
	/// @param out Receives the graph on success.
	/// @param error Receives "graph '<path>' not found" or "graph '<path>' is invalid: <reason>" on failure.
	/// @return True if the graph was read and parsed (it may still have validation issues).
	bool LoadGraphAsset(const String& graphPath, pcg::graph::Graph& out, String& error);

	/// @brief Whether the graph has a node whose type has a "Terrain" output pin (it can write terrain).
	bool GraphHasTerrainOps(const pcg::graph::Graph& graph);

	/// @brief The areas of the graph instances that share seams with `record` (see PreparedTerrain::Prepare): every other
	///        instance that is a terrain writer (phase 3a rule R11: it has written terrain, or its graph has terrain
	///        ops), sorted by id.
	/// @param record The instance.
	/// @param instances The world's graph instances (may include the record itself, which is skipped).
	/// @param areas Cleared, then receives the areas.
	/// @param names If not null, cleared, then receives the instance names, parallel to areas.
	void CollectSeamNeighbours(const pcg::GraphInstanceRecord& record, const std::vector<const pcg::GraphInstanceRecord*>& instances,
		std::vector<PathSpline>& areas, std::vector<String>* names = nullptr);

	/// @brief Joins validation issues into one message: "node <id> (<type>): <message>" per issue ("graph: <message>"
	///        for graph-level issues), separated by "; ".
	String DescribeGraphIssues(const pcg::graph::Graph& graph, const std::vector<pcg::graph::GraphIssue>& issues);

	/// @brief Parses a graph parameter value from text, typed by the parameter's default value: Float and Int as
	///        numbers, Bool as true/false/1/0, Vector3 as "x,y,z", String as is.
	/// @param parameter The parameter the value is for.
	/// @param text The text to parse.
	/// @param out Receives the value on success.
	/// @param error Receives the reason on failure.
	/// @return True if the text is a valid value of the parameter's type.
	bool ParseGraphParameterValue(const pcg::graph::ParameterDef& parameter, const String& text, pcg::graph::AttributeValue& out, String& error);

	/// @brief Builds the evaluation context of a graph instance. Resolves the placed controls (problems become
	///        warnings, the controls that failed are listed in out.unresolved), loads the terrain pages under the area and under every resolved control whose bounds reach into
	///        it, plus a margin (page streaming does not change the terrain revision, so the sampler would not notice
	///        them otherwise), and creates the editor terrain sampler. The graph
	///        is not read here, so a caller can evaluate an in-memory graph with the same context.
	/// @param editor The world editor of the instance. Must have a terrain.
	/// @param record The instance.
	/// @param controls The procedural mode holding the world's placed controls.
	/// @param out Receives the context; must be freshly constructed.
	/// @param error Receives the reason when the call returns false.
	/// @return False if the area is not a closed loop of at least 3 points, the world has no terrain or the pages under
	///         the area could not be loaded.
	bool BuildGraphInstanceContext(IWorldEditor& editor, const pcg::GraphInstanceRecord& record, ProceduralEditMode& controls,
		GraphInstanceContext& out, String& error);

	/// @brief The terrain a rebuild of the instance would evaluate on: a BaseTerrainSampler over `live` undoing the
	///        record's owned terrain contribution.
	/// @param editor The world editor of the instance. Must have a terrain.
	/// @param record The instance.
	/// @param live The live terrain sampler. Must outlive the result.
	/// @return Null if the record owns no terrain contribution (the live terrain is the base then).
	std::unique_ptr<BaseTerrainSampler> MakeBaseTerrainSampler(IWorldEditor& editor, const pcg::GraphInstanceRecord& record,
		const EditorTerrainSampler& live);

	/// @brief Warning UseBaseTerrain adds when the contribution changed tile materials (not undone by the base view).
	constexpr const char* BaseTerrainMaterialWarning =
		"the instance's own tile material changes are not reflected in the preview (the base terrain view keeps them)";

	/// @brief Makes a built context evaluate on the base terrain (see MakeBaseTerrainSampler): sets context.baseSampler
	///        and points context.context.terrain at it, and adds BaseTerrainMaterialWarning to context.warnings when the
	///        contribution changed tile materials. Leaves the context on the live terrain if the record owns no
	///        terrain contribution.
	/// @param editor The world editor the context was built for.
	/// @param record The instance the context was built for.
	/// @param context A context built by BuildGraphInstanceContext.
	/// @return True if the context now evaluates on the base terrain.
	bool UseBaseTerrain(IWorldEditor& editor, const pcg::GraphInstanceRecord& record, GraphInstanceContext& context);

	/// @brief Prepares the terrain ops one node of an evaluation produced (the items of the "Terrain" pins whose
	///        op.order is the node id, see pcg::graph::CollectTerrainOps), the way a rebuild prepares them: on the
	///        given ground, clipped to the instance's area with its borderFade, section assets read like the rebuild
	///        reads them (a missing, unreadable or stale asset fails), through the caller's cache.
	/// @param graph The evaluated graph.
	/// @param result Its evaluation result.
	/// @param nodeId The node whose ops to prepare.
	/// @param record The instance.
	/// @param ground Base ground height at a world position.
	/// @param sections Supplies the section assets (kept across calls by the caller).
	/// @param out Receives the prepared ops.
	/// @param error Receives the reason (naming the node and asset) when the call returns false.
	/// @param neighbours Areas of the neighbouring terrain-writing instances (CollectSeamNeighbours).
	/// @return False if the ops could not be prepared.
	bool PrepareNodeTerrainOps(const pcg::graph::Graph& graph, const pcg::graph::EvalResult& result, uint32 nodeId,
		const pcg::GraphInstanceRecord& record, const std::function<float(float, float)>& ground, SectionAssetCache& sections,
		pcg::graph::PreparedTerrain& out, String& error, const std::vector<PathSpline>& neighbours);

	/// @brief Reads the instance's saved graph, builds its context and evaluates it, without changing the world. When the
	///        graph validates, the spawned points are collected and clipped to the area.
	/// @param editor The world editor of the instance.
	/// @param record The instance.
	/// @param controls The procedural mode holding the world's placed controls.
	/// @param out Receives the evaluation.
	/// @param error Receives the reason when the call returns false.
	/// @param baseTerrain True evaluates on the base terrain a rebuild would see (UseBaseTerrain), false on the live
	///        terrain.
	/// @return False if the graph could not be read or parsed, or the context could not be built. Validation issues and
	///         node errors do not fail the call; they are in out.result.
	bool EvaluateGraphInstance(IWorldEditor& editor, const pcg::GraphInstanceRecord& record, ProceduralEditMode& controls,
		GraphInstanceEvaluation& out, String& error, bool baseTerrain = false);

	/// @brief A graph instance rebuild in three steps, so that the expensive middle step can run on a worker thread while
	///        the editor stays responsive (ProceduralEditMode's background rebuilds). RebuildGraphInstance runs the same
	///        steps on the calling thread, so both write the same.
	/// @details Start (main thread) refuses what it can decide before evaluating, loads the pages, copies what the
	///          evaluation reads (the record, the graph, the placed controls, the tile materials) and takes the instance's
	///          previous terrain contribution out of the terrain data (BeginContentRebuild: the world keeps showing the old
	///          build). Run (one thread at a time) evaluates the graph on that base terrain, prepares its terrain ops and
	///          computes every terrain sample; it only reads the terrain, so nothing may write the terrain until Commit or
	///          Abort, and the editor must not change the terrain pages it reads (they stay prepared: streaming only unloads
	///          tiles). Commit (main thread) decides the remaining refusals, writes the result and reconciles the placements
	///          (FinishContentRebuild); Abort puts the old contribution back. Every job must end in Commit or Abort.
	class GraphRebuildJob final : public NonCopyable
	{
	public:
		~GraphRebuildJob();

		/// @brief Starts a rebuild (main thread). Refusals here (bad area, area outside the terrain or overlapping another
		///        instance, unreadable graph, pages that cannot be loaded, a record that no longer matches the terrain)
		///        leave the world and record untouched. On success record.content.terrain is empty until Commit or Abort.
		/// @return The job, or null with the reason in error.
		static std::unique_ptr<GraphRebuildJob> Start(IWorldEditor& editor, pcg::GraphInstanceRecord& record,
			const std::vector<const pcg::GraphInstanceRecord*>& otherInstances, ProceduralEditMode& controls, String& error);

		/// @brief Evaluates, prepares the terrain ops and computes the terrain samples (any one thread; see the class).
		///        Returns early once canceled. If a section asset was not loaded yet, or a Paint op switches tiles to a
		///        tile material whose layer textures are not read yet, NeedsSectionAssets is true afterwards: call
		///        LoadSectionAssets on the main thread and Run again, until it is false (each run stops at the first
		///        missing section asset, or after resolving the tile materials; each round loads at least one).
		void Run();

		/// @brief Whether the last Run stopped for section assets or tile materials that are not loaded yet.
		[[nodiscard]] bool NeedsSectionAssets() const;

		/// @brief Loads the section assets and the layer textures of the tile materials the last Run asked for (main
		///        thread).
		void LoadSectionAssets();

		/// @brief Asks Run to stop at its next step; Commit then refuses. Any thread.
		void Cancel();

		/// @brief Whether Cancel was called.
		[[nodiscard]] bool IsCanceled() const;

		/// @brief Ends the job with its result (main thread): the refusals of RebuildGraphInstance that need the evaluation
		///        (canceled, terrain changed since Start, validation issues, node errors, missing spawn assets, unresolved
		///        controls reaching the area, terrain ops that cannot be prepared, an overlap found now) abort it and
		///        leave the world as before; otherwise the terrain is written, the placements reconciled and the river flow
		///        and seams updated (see RebuildGraphInstance). Write rects over open ocean are materialised first and the
		///        job runs again on this thread.
		/// @param record The instance the job was started for (the same object).
		GeneratorBuildReport Commit(IWorldEditor& editor, pcg::GraphInstanceRecord& record, const std::vector<const pcg::GraphInstanceRecord*>& otherInstances,
			ProceduralEditMode& controls, const std::function<bool(const pcg::WorldRect& areaRect, String& error)>& checkTerrainOverlap);

		/// @brief Ends the job without a result (main thread): the old contribution is put back.
		/// @param record The instance the job was started for (the same object).
		void Abort(pcg::GraphInstanceRecord& record);

		/// @brief Id of the instance being rebuilt.
		[[nodiscard]] uint64 GetInstanceId() const;

		/// @brief Seconds since Start.
		[[nodiscard]] double GetElapsedSeconds() const;

	private:
		GraphRebuildJob();

		struct Impl;
		std::unique_ptr<Impl> m_impl;
	};

	/// @brief Rebuilds a graph instance: the steps of a GraphRebuildJob, run on the calling thread.
	/// @details Refused, with the world and record ending as before (the old contribution is put back exactly, hand
	///          sculpting included): an area wholly outside the terrain, missing graph, parse error, validation issues
	///          (message lists them), evaluation error, overlap with another instance's area, missing spawn assets, a placed
	///          control that failed to resolve (missing, renamed or invalid type, geometry not fitting the shape) whose
	///          bounds reach the area or cannot be computed (the evaluation would silently run without it and the reconcile
	///          would delete its content; the message names those controls), and terrain ops that cannot be prepared
	///          (missing, unreadable or stale section asset, a section param it rejects; the message names the node and
	///          asset). The previous contribution is taken out of the terrain data first (BeginContentRebuild), the graph is
	///          evaluated on that base terrain, its terrain ops are prepared on it (pcg::graph::PreparedTerrain, clipped to
	///          the area with the record's borderFade) and every terrain sample is computed; then they are written (an
	///          op-free graph writes nothing). Write rects over open ocean are materialised and the computation repeated on
	///          the materialised base. Spawned placements keep their height above the base ground on the final ground; the
	///          section placements of the ops are added under salted keys. After a successful write the river paths of the
	///          applied terrain (PreparedTerrain::GetFlowPaths) join the world's river flow: Terrain::ApplyWaterFlow over the hand rivers
	///          the editor holds (IWorldEditor::GetHandFlowPaths, else the .hflow) followed by every instance's paths (ProceduralEditMode::CollectGeneratedFlowPaths,
	///          this one's new), with this instance's old paths as the previous set; they are then stored in
	///          record.flowPaths. Bit-identical paths skip that step. A refused or failed rebuild touches no flow.
	/// @param editor The world editor to work on. Must have a terrain.
	/// @param record The instance to rebuild; its owned content and flow paths are updated.
	/// @param otherInstances The world's other graph instances (their areas must not overlap this one's).
	/// @param controls The procedural mode holding the world's placed controls.
	/// @param checkTerrainOverlap If set, called with the area's bounds when the graph has terrain ops (before anything
	///        is written); returning false refuses the rebuild with its error (the procedural mode checks the bounds
	///        against what oases and section paths built).
	/// @return The report. Warnings: control resolution, parameter merge, node and collection warnings of the run on
	///         the base terrain, terrain op preparation and section placement warnings, and paint skipped on tiles
	///         without the texture (Paint ops and section paint, one warning per op). Paint ops with a tileMaterial
	///         switch every tile they paint on (pcg::graph::PreparedTerrain::ResolveTileMaterials over the area's
	///         bounds) and paint against that material's layers; warned: a tile two such ops ask different materials
	///         for, a switched tile where a layer slot whose texture the switch changes had weight before the build
	///         outside the paint of those ops, and a tile material that could not be loaded (its tiles are not
	///         switched).
	GeneratorBuildReport RebuildGraphInstance(IWorldEditor& editor, pcg::GraphInstanceRecord& record,
		const std::vector<const pcg::GraphInstanceRecord*>& otherInstances, ProceduralEditMode& controls,
		const std::function<bool(const pcg::WorldRect& areaRect, String& error)>& checkTerrainOverlap = {});

	/// @brief Removes everything a graph instance put into the world (RemoveGeneratedContent on its owned content),
	///        then its river paths from the world's flow (as a rebuild with no paths does, see RebuildGraphInstance).
	///        Leaves the record's owned content and flow paths empty (area, graph, seed and overrides are kept).
	/// @param editor The world editor to work on.
	/// @param record The instance whose content to delete.
	/// @param controls The procedural mode holding the world's graph instances (for the other instances' paths).
	/// @return The report (removed = number of owned objects deleted).
	GeneratorBuildReport DeleteGraphInstanceContent(IWorldEditor& editor, pcg::GraphInstanceRecord& record, const ProceduralEditMode& controls);
}
