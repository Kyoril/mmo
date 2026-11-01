// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "editor_terrain_sampler.h"
#include "generated_content.h"

#include "base/typedefs.h"
#include "pcg/graph_instance_record.h"
#include "pcg/graph/controls.h"
#include "pcg/graph/evaluator.h"
#include "pcg/graph/graph.h"
#include "pcg/graph/graph_output.h"
#include "pcg/graph/node_util.h"

#include <functional>
#include <memory>
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

		/// @brief Terrain sampler context.terrain points at.
		std::unique_ptr<EditorTerrainSampler> sampler;
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

	/// @brief Reads and parses a graph asset (.hpcgg).
	/// @param graphPath Asset path of the graph.
	/// @param out Receives the graph on success.
	/// @param error Receives "graph '<path>' not found" or "graph '<path>' is invalid: <reason>" on failure.
	/// @return True if the graph was read and parsed (it may still have validation issues).
	bool LoadGraphAsset(const String& graphPath, pcg::graph::Graph& out, String& error);

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

	/// @brief Reads the instance's saved graph, builds its context and evaluates it, without changing the world. When the
	///        graph validates, the spawned points are collected and clipped to the area.
	/// @param editor The world editor of the instance.
	/// @param record The instance.
	/// @param controls The procedural mode holding the world's placed controls.
	/// @param out Receives the evaluation.
	/// @param error Receives the reason when the call returns false.
	/// @return False if the graph could not be read or parsed, or the context could not be built. Validation issues and
	///         node errors do not fail the call; they are in out.result.
	bool EvaluateGraphInstance(IWorldEditor& editor, const pcg::GraphInstanceRecord& record, ProceduralEditMode& controls,
		GraphInstanceEvaluation& out, String& error);

	/// @brief Rebuilds a graph instance: terrain ops and placements through ApplyGeneratedContent.
	/// @details First a dry run on the live terrain decides every refusal, leaving the world and record untouched:
	///          missing graph, parse error, validation issues (message lists them), evaluation error, overlap with
	///          another instance's area, missing spawn assets, a placed control that failed to resolve (missing,
	///          renamed or invalid type, geometry not fitting the shape) whose bounds reach the area or cannot be
	///          computed (the evaluation would silently run without it and the reconcile would delete its content; the
	///          message names those controls), and terrain ops that cannot be prepared (missing, unreadable or stale
	///          section asset, a section param it rejects; the message names the node and asset).
	///          Then ApplyGeneratedContent undoes the previous contribution and, in content.prepare, the graph is
	///          evaluated again on that base terrain, its terrain ops are prepared on it (pcg::graph::PreparedTerrain,
	///          clipped to the area with the record's borderFade) and written; an op-free graph writes nothing. Should
	///          that second run fail after all, the old contribution is put back (the world ends as before). Spawned
	///          placements keep their height above the base ground on the final ground; the section placements of
	///          the ops are added under salted keys.
	/// @param editor The world editor to work on. Must have a terrain.
	/// @param record The instance to rebuild; its owned content is updated.
	/// @param otherInstances The world's other graph instances (their areas must not overlap this one's).
	/// @param controls The procedural mode holding the world's placed controls.
	/// @param checkTerrainOverlap If set, called with the area's bounds when the graph has terrain ops (part of the
	///        dry run); returning false refuses the rebuild with its error (the procedural mode checks the bounds
	///        against what oases and section paths built).
	/// @return The report. Warnings: control resolution, parameter merge, node and collection warnings of the run on
	///         the base terrain, terrain op preparation and section placement warnings, and paint skipped on tiles
	///         without the texture (Paint ops and section paint, one warning per op).
	GeneratorBuildReport RebuildGraphInstance(IWorldEditor& editor, pcg::GraphInstanceRecord& record,
		const std::vector<const pcg::GraphInstanceRecord*>& otherInstances, ProceduralEditMode& controls,
		const std::function<bool(const pcg::WorldRect& areaRect, String& error)>& checkTerrainOverlap = {});

	/// @brief Removes everything a graph instance put into the world (RemoveGeneratedContent on its owned content).
	///        Leaves the record's owned content empty (area, graph, seed and overrides are kept).
	/// @param editor The world editor to work on.
	/// @param record The instance whose content to delete.
	/// @return The report (removed = number of owned objects deleted).
	GeneratorBuildReport DeleteGraphInstanceContent(IWorldEditor& editor, pcg::GraphInstanceRecord& record);
}
