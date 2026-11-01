// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "graph_instance_builder.h"

#include "text_parsing.h"

#include "assets/asset_registry.h"
#include "editors/world_editor/edit_modes/procedural_edit_mode.h"
#include "editors/world_editor/edit_modes/world_edit_mode.h"
#include "log/default_log_levels.h"
#include "pcg/graph/graph_io.h"
#include "pcg/graph/node_util.h"
#include "pcg/graph/node_registry.h"
#include "terrain/constants.h"
#include "terrain/terrain.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <set>

namespace mmo
{
	namespace
	{
		/// Generator kind shown in the names of created map entities.
		const String GraphInstanceLabel = "Graph";

		using Clock = std::chrono::steady_clock;

		double MillisecondsSince(const Clock::time_point start)
		{
			return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
		}

		String FormatId(const uint64 id)
		{
			char buffer[32];
			std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(id));
			return String(buffer);
		}

		/// x/z bounds of the area's control points (the Catmull-Rom loop passes through them; its bulge between
		/// points is covered by the page margin of the callers).
		pcg::WorldRect GetAreaBounds(const PathSpline& area)
		{
			pcg::WorldRect rect{ area.points.front().x, area.points.front().z, area.points.front().x, area.points.front().z };
			for (const SplinePoint& point : area.points)
			{
				rect.minX = std::min(rect.minX, point.x);
				rect.minZ = std::min(rect.minZ, point.z);
				rect.maxX = std::max(rect.maxX, point.x);
				rect.maxZ = std::max(rect.maxZ, point.z);
			}
			return rect;
		}

		/// Conservative x/z bounds of a resolved control: its spline (loops and splines), its box footprint at any
		/// yaw (volumes) or its position (points). Invalid without a type or instance.
		pcg::graph::Bounds2D GetControlBounds(const pcg::graph::ResolvedControl& control)
		{
			pcg::graph::Bounds2D bounds;
			if (!control.type || !control.instance)
			{
				return bounds;
			}

			const pcg::graph::ControlInstance& instance = *control.instance;
			switch (control.type->shape)
			{
			case pcg::graph::ControlShape::ClosedLoop:
			case pcg::graph::ControlShape::OpenSpline:
				return pcg::graph::SplineBounds(instance.spline);

			case pcg::graph::ControlShape::Volume:
			{
				// The footprint's diagonal radius covers every yaw.
				const float radius = std::sqrt(instance.halfExtents.x * instance.halfExtents.x + instance.halfExtents.z * instance.halfExtents.z);
				bounds.Add(instance.position.x - radius, instance.position.z - radius);
				bounds.Add(instance.position.x + radius, instance.position.z + radius);
				return bounds;
			}

			case pcg::graph::ControlShape::Point:
				bounds.Add(instance.position.x, instance.position.z);
				return bounds;
			}
			return bounds;
		}

		bool CheckArea(const PathSpline& area, String& error)
		{
			if (!area.closed || area.points.size() < 3)
			{
				error = "the area must be a closed loop of at least 3 points";
				return false;
			}

			for (const SplinePoint& point : area.points)
			{
				if (!std::isfinite(point.x) || !std::isfinite(point.z))
				{
					error = "the area has a point that is not finite";
					return false;
				}
			}
			return true;
		}

		const char* FindNodeType(const pcg::graph::Graph& graph, const uint32 nodeId)
		{
			for (const pcg::graph::NodeInstance& node : graph.nodes)
			{
				if (node.id == nodeId)
				{
					return node.type.c_str();
				}
			}
			return "?";
		}
	}

	bool LoadGraphAsset(const String& graphPath, pcg::graph::Graph& out, String& error)
	{
		if (graphPath.empty())
		{
			error = "no graph is set";
			return false;
		}

		const std::unique_ptr<std::istream> file = AssetRegistry::OpenFile(graphPath);
		if (!file)
		{
			error = "graph '" + graphPath + "' not found";
			return false;
		}

		const String text((std::istreambuf_iterator<char>(*file)), std::istreambuf_iterator<char>());
		String parseError;
		if (!pcg::graph::ParseGraph(text, out, parseError))
		{
			error = "graph '" + graphPath + "' is invalid: " + parseError;
			return false;
		}
		return true;
	}

	String DescribeGraphIssues(const pcg::graph::Graph& graph, const std::vector<pcg::graph::GraphIssue>& issues)
	{
		String text;
		for (const pcg::graph::GraphIssue& issue : issues)
		{
			text += text.empty() ? "" : "; ";
			if (issue.node == 0)
			{
				text += "graph: " + issue.message;
			}
			else
			{
				text += "node " + std::to_string(issue.node) + " (" + FindNodeType(graph, issue.node) + "): " + issue.message;
			}
		}
		return text;
	}

	bool ParseGraphParameterValue(const pcg::graph::ParameterDef& parameter, const String& text, pcg::graph::AttributeValue& out, String& error)
	{
		switch (pcg::graph::TypeOf(parameter.defaultValue))
		{
		case pcg::graph::AttributeType::Float:
		{
			float value = 0.0f;
			if (!ParseFloatText(text, value))
			{
				error = "parameter '" + parameter.name + "' needs a number, got '" + text + "'";
				return false;
			}
			out = value;
			return true;
		}

		case pcg::graph::AttributeType::Int:
		{
			errno = 0;
			char* end = nullptr;
			const long long value = std::strtoll(text.c_str(), &end, 10);
			if (text.empty() || end != text.c_str() + text.size() || errno == ERANGE)
			{
				error = "parameter '" + parameter.name + "' needs an integer, got '" + text + "'";
				return false;
			}
			out = static_cast<int64>(value);
			return true;
		}

		case pcg::graph::AttributeType::Bool:
			if (text == "true" || text == "1")
			{
				out = true;
				return true;
			}
			if (text == "false" || text == "0")
			{
				out = false;
				return true;
			}
			error = "parameter '" + parameter.name + "' needs true or false, got '" + text + "'";
			return false;

		case pcg::graph::AttributeType::Vector3:
		{
			float components[3] = {};
			size_t start = 0;
			for (int i = 0; i < 3; ++i)
			{
				const size_t comma = i < 2 ? text.find(',', start) : text.size();
				if (comma == String::npos || !ParseFloatText(text.substr(start, comma - start), components[i]))
				{
					error = "parameter '" + parameter.name + "' needs x,y,z, got '" + text + "'";
					return false;
				}
				start = comma + 1;
			}
			out = Vector3(components[0], components[1], components[2]);
			return true;
		}

		case pcg::graph::AttributeType::String:
			out = text;
			return true;
		}

		error = "parameter '" + parameter.name + "' has an unknown type";
		return false;
	}

	bool BuildGraphInstanceContext(IWorldEditor& editor, const pcg::GraphInstanceRecord& record, ProceduralEditMode& controls,
		GraphInstanceContext& out, String& error)
	{
		if (!CheckArea(record.area, error))
		{
			return false;
		}

		terrain::Terrain* terrain = editor.GetTerrain();
		if (!terrain || !editor.HasTerrain())
		{
			error = "the world has no terrain";
			return false;
		}

		controls.ResolvePlacedControls(out.controls, out.warnings);

		// Streaming a page in does not bump the terrain revision, so a sampler would not notice newly loaded pages:
		// load everything the evaluation may read first. That is the area and every control reaching into it
		// (samplers cover whole controls, and pruning near the border needs real heights outside the area too),
		// with a margin.
		const pcg::graph::Bounds2D areaBounds = pcg::graph::SplineBounds(record.area);
		pcg::graph::Bounds2D bounds = areaBounds;
		for (const pcg::graph::ResolvedControl& control : out.controls)
		{
			const pcg::graph::Bounds2D controlBounds = GetControlBounds(control);
			if (controlBounds.valid && pcg::graph::SaneBounds(controlBounds) && controlBounds.Intersects(areaBounds))
			{
				bounds.Add(controlBounds.minX, controlBounds.minZ);
				bounds.Add(controlBounds.maxX, controlBounds.maxZ);
			}
		}

		const float margin = static_cast<float>(terrain::constants::PageSize / 8.0);
		String loadError;
		if (!editor.EnsurePagesLoaded(bounds.minX - margin, bounds.minZ - margin, bounds.maxX + margin, bounds.maxZ + margin, loadError))
		{
			error = "the terrain under the area and its controls could not be loaded: " + loadError;
			return false;
		}
		out.sampler = std::make_unique<EditorTerrainSampler>(*terrain, [&editor]() { return editor.GetTerrainRevision(); });

		out.context = pcg::graph::EvalContext();
		out.context.instanceSeed = record.seed;
		out.context.region = record.area;
		out.context.parameters = record.parameterOverrides;
		out.context.controls = &out.controls;
		// 0: Evaluate hashes the controls itself.
		out.context.controlsRevision = 0;
		out.context.terrain = out.sampler.get();
		out.context.pointLimit = record.pointLimit;
		return true;
	}

	bool EvaluateGraphInstance(IWorldEditor& editor, const pcg::GraphInstanceRecord& record, ProceduralEditMode& controls,
		GraphInstanceEvaluation& out, String& error)
	{
		if (!LoadGraphAsset(record.graphPath, out.graph, error))
		{
			return false;
		}

		GraphInstanceContext context;
		if (!BuildGraphInstanceContext(editor, record, controls, context, error))
		{
			return false;
		}

		out.warnings = context.warnings;
		pcg::graph::MergeParameters(out.graph.parameters, record.parameterOverrides, &out.warnings);

		out.result = pcg::graph::Evaluate(out.graph, pcg::graph::NodeRegistry::Default(), context.context, nullptr);
		if (out.result.issues.empty())
		{
			out.collected = pcg::graph::CollectPlacements(out.graph, out.result, record.area);
			out.warnings.insert(out.warnings.end(), out.collected.warnings.begin(), out.collected.warnings.end());
		}
		return true;
	}

	GeneratorBuildReport RebuildGraphInstance(IWorldEditor& editor, pcg::GraphInstanceRecord& record,
		const std::vector<const pcg::GraphInstanceRecord*>& otherInstances, ProceduralEditMode& controls)
	{
		const Clock::time_point start = Clock::now();
		GeneratorBuildReport report;
		const auto refuse = [&report, &start, &record](const String& reason)
		{
			report.error = "Rebuild of '" + record.name + "' refused, the world was not changed: " + reason;
			report.milliseconds = MillisecondsSince(start);
			ELOG("PCG: " << report.error);
			return report;
		};

		String error;
		if (!CheckArea(record.area, error))
		{
			return refuse(error);
		}

		for (const pcg::GraphInstanceRecord* other : otherInstances)
		{
			if (other && other->id != record.id && pcg::AreasOverlap(record.area, other->area))
			{
				return refuse("its area overlaps the area of graph instance '" + other->name + "' (" + FormatId(other->id) + ")");
			}
		}

		GraphInstanceEvaluation evaluation;
		if (!EvaluateGraphInstance(editor, record, controls, evaluation, error))
		{
			return refuse(error);
		}

		if (!evaluation.result.issues.empty())
		{
			return refuse("the graph has validation issues: " + DescribeGraphIssues(evaluation.graph, evaluation.result.issues));
		}

		std::vector<String> warnings = evaluation.warnings;
		String nodeErrors;
		for (const auto& [nodeId, nodeResult] : evaluation.result.nodes)
		{
			const String prefix = "node " + std::to_string(nodeId) + " (" + FindNodeType(evaluation.graph, nodeId) + "): ";
			if (!nodeResult.output.error.empty())
			{
				nodeErrors += (nodeErrors.empty() ? "" : "; ") + prefix + nodeResult.output.error;
			}
			for (const String& warning : nodeResult.output.warnings)
			{
				warnings.push_back(prefix + warning);
			}
		}
		if (!nodeErrors.empty())
		{
			return refuse("the evaluation failed: " + nodeErrors);
		}

		// Every asset must exist: a placement that cannot be created would not be owned and be retried forever.
		std::set<String> missingAssets;
		for (const pcg::Placement& placement : evaluation.collected.placements)
		{
			if (!AssetRegistry::HasFile(placement.asset))
			{
				missingAssets.insert(placement.asset);
			}
		}
		if (!missingAssets.empty())
		{
			String list;
			for (const String& asset : missingAssets)
			{
				list += (list.empty() ? "" : ", ") + asset;
			}
			return refuse("missing spawn asset(s): " + list);
		}

		// Placements only: the terrain step is skipped (no terrain target), the positions come from the graph.
		GeneratedContent content;
		content.influence = GetAreaBounds(record.area);
		const std::vector<pcg::Placement>& placements = evaluation.collected.placements;
		content.placements = [&placements](const GroundFunction&, const GroundFunction&, std::vector<pcg::Placement>& out)
		{
			out = placements;
		};

		report = ApplyGeneratedContent(editor, content, record.content, record.id, GraphInstanceLabel);
		report.warnings.insert(report.warnings.end(), warnings.begin(), warnings.end());
		report.milliseconds = MillisecondsSince(start);
		if (!report.ok)
		{
			report.error = "Rebuild of '" + record.name + "' failed: " + report.error;
		}
		else
		{
			ILOG("PCG: graph instance '" << record.name << "' (" << FormatId(record.id) << "): " << placements.size()
				<< " placement(s) inside the area, " << evaluation.collected.clipped << " clipped");
		}
		return report;
	}

	GeneratorBuildReport DeleteGraphInstanceContent(IWorldEditor& editor, pcg::GraphInstanceRecord& record)
	{
		return RemoveGeneratedContent(editor, record.content);
	}
}
