// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "graph_instance_builder.h"

#include "editor_terrain_access.h"
#include "editor_texture_layers.h"
#include "section_path_builder.h"
#include "text_parsing.h"
#include "editors/world_editor/flow_paths_file.h"

#include "assets/asset_registry.h"
#include "editors/world_editor/edit_modes/procedural_edit_mode.h"
#include "editors/world_editor/edit_modes/world_edit_mode.h"
#include "log/default_log_levels.h"
#include "base/non_copyable.h"
#include "pcg/graph/graph_io.h"
#include "pcg/graph/node_util.h"
#include "pcg/graph/node_registry.h"
#include "pcg/graph/terrain_compose.h"
#include "pcg/parallel_for.h"
#include "pcg/pcg_random.h"
#include "terrain/constants.h"
#include "terrain/terrain.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <thread>

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

		/// Salt of the keys of section placements, keeping them apart from the spawn keys (CollectPlacements).
		constexpr uint64 SectionPlacementKeySalt = 0x5EC7A11Du;

		/// The rect clipped to the world extent of the terrain (min > max when it lies wholly outside).
		pcg::WorldRect ClampToTerrain(const pcg::WorldRect& rect, const terrain::Terrain& terrain)
		{
			const pcg::WorldRect extent = GetTerrainRect(terrain);
			return pcg::WorldRect{ std::max(rect.minX, extent.minX), std::max(rect.minZ, extent.minZ), std::min(rect.maxX, extent.maxX),
				std::min(rect.maxZ, extent.maxZ) };
		}

		/// Conservative x/z bounds of the area (pcg::graph::SplineBounds: its points and tangent handles).
		pcg::WorldRect GetAreaRect(const PathSpline& area)
		{
			const pcg::graph::Bounds2D bounds = pcg::graph::SplineBounds(area);
			return pcg::WorldRect{ bounds.minX, bounds.minZ, bounds.maxX, bounds.maxZ };
		}

		/// Box footprint of a volume at any yaw: the footprint's diagonal radius covers every rotation.
		pcg::graph::Bounds2D GetVolumeFootprint(const pcg::graph::ControlInstance& instance)
		{
			pcg::graph::Bounds2D bounds;
			const float radius = std::sqrt(instance.halfExtents.x * instance.halfExtents.x + instance.halfExtents.z * instance.halfExtents.z);
			bounds.Add(instance.position.x - radius, instance.position.z - radius);
			bounds.Add(instance.position.x + radius, instance.position.z + radius);
			return bounds;
		}

		/// Conservative x/z bounds of a resolved control. Invalid without a type or instance.
		pcg::graph::Bounds2D GetControlBounds(const pcg::graph::ResolvedControl& control)
		{
			if (!control.type || !control.instance)
			{
				return pcg::graph::Bounds2D();
			}
			return GetPlacedControlBounds(*control.instance, control.type);
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

		/// Evaluates out.graph (already read) with a freshly built context, the way EvaluateGraphInstance does.
		bool EvaluateLoadedGraph(IWorldEditor& editor, const pcg::GraphInstanceRecord& record, ProceduralEditMode& controls,
			GraphInstanceEvaluation& out, String& error, const bool baseTerrain = false)
		{
			GraphInstanceContext context;
			if (!BuildGraphInstanceContext(editor, record, controls, context, error))
			{
				return false;
			}
			if (baseTerrain)
			{
				UseBaseTerrain(editor, record, context);
			}

			out.warnings = context.warnings;
			out.unresolved = context.unresolved;
			pcg::graph::MergeParameters(out.graph.parameters, record.parameterOverrides, &out.warnings);

			out.result = pcg::graph::Evaluate(out.graph, pcg::graph::NodeRegistry::Default(), context.context, nullptr);
			if (out.result.issues.empty())
			{
				out.collected = pcg::graph::CollectPlacements(out.graph, out.result, record.area);
				out.warnings.insert(out.warnings.end(), out.collected.warnings.begin(), out.collected.warnings.end());
			}
			return true;
		}

		/// The refusal checks of an evaluation: placed controls that may reach the area but failed to resolve,
		/// validation issues, node errors and missing spawn assets. Fills warnings with the evaluation's warnings
		/// and the node warnings.
		bool CheckEvaluation(const GraphInstanceEvaluation& evaluation, const PathSpline& area, std::vector<String>& warnings, String& error)
		{
			// A control the evaluation had to skip silently drops out of the result, and the reconcile would delete
			// what it produced (renaming a control type would wipe a forest). Only controls that cannot reach the area
			// are harmless; unknown bounds count as reaching.
			const pcg::graph::Bounds2D areaBounds = pcg::graph::SplineBounds(area);
			String unresolved;
			for (const UnresolvedPlacedControl& control : evaluation.unresolved)
			{
				const bool known = control.bounds.valid && pcg::graph::SaneBounds(control.bounds);
				if (!known || control.bounds.Intersects(areaBounds))
				{
					unresolved += (unresolved.empty() ? "" : "; ") + ("'" + control.name + "' (" + FormatId(control.id) + "): " + control.reason);
				}
			}
			if (!unresolved.empty())
			{
				error = "placed control(s) that may reach the area could not be resolved: " + unresolved
					+ ". Restore or fix their control types (or move or delete the controls), then rebuild again.";
				return false;
			}

			if (!evaluation.result.issues.empty())
			{
				error = "the graph has validation issues: " + DescribeGraphIssues(evaluation.graph, evaluation.result.issues);
				return false;
			}

			warnings = evaluation.warnings;
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
				error = "the evaluation failed: " + nodeErrors;
				return false;
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
				error = "missing spawn asset(s): " + list;
				return false;
			}
			return true;
		}

		/// Prepares the evaluation's terrain ops (see PreparedTerrain::Prepare); the error names the node and asset.
		bool PrepareTerrainOps(const GraphInstanceEvaluation& evaluation, const pcg::GraphInstanceRecord& record,
			const GroundFunction& ground, SectionAssetCache& sections, const std::vector<PathSpline>& neighbours,
			pcg::graph::PreparedTerrain& out, String& error)
		{
			String prepareError;
			if (!out.Prepare(pcg::graph::CollectTerrainOps(evaluation.graph, evaluation.result), ground, sections.MakeProvider(),
				record.area, record.borderFade, prepareError, neighbours))
			{
				error = "the terrain ops could not be prepared: " + prepareError;
				return false;
			}
			return true;
		}

		static_assert(sizeof(terrain_io::water_flow::FlowPoint) == 3 * sizeof(float), "FlowPoint must be three packed floats");

		/// Whether two path sets are bit-identical.
		bool SameFlowPaths(const std::vector<terrain_io::water_flow::FlowPath>& a, const std::vector<terrain_io::water_flow::FlowPath>& b)
		{
			if (a.size() != b.size())
			{
				return false;
			}
			for (size_t i = 0; i < a.size(); ++i)
			{
				if (a[i].size() != b[i].size() ||
					(!a[i].empty() && std::memcmp(a[i].data(), b[i].data(), a[i].size() * sizeof(terrain_io::water_flow::FlowPoint)) != 0))
				{
					return false;
				}
			}
			return true;
		}

		/// Makes the instance's river paths newPaths: recomputes the world's river flow from the hand rivers (the open
		/// editor's via GetHandFlowPaths, else the world's .hflow in file order) followed by every graph instance's paths (this one's replaced by newPaths), with this
		/// instance's old paths as the previous set, then stores newPaths in the record. Bit-identical paths change
		/// nothing. Call it only after the terrain write (or removal) succeeded.
		/// @return False (with a warning, the record and the flow left as they were) if the .hflow file is needed and cannot be read.
		bool ApplyInstanceFlow(IWorldEditor& editor, pcg::GraphInstanceRecord& record, const ProceduralEditMode& controls,
			std::vector<terrain_io::water_flow::FlowPath> newPaths, std::vector<String>& warnings)
		{
			if (SameFlowPaths(record.flowPaths, newPaths))
			{
				return true;
			}

			terrain::Terrain* terrain = editor.GetTerrain();
			if (!terrain)
			{
				// No pages, no flow to write.
				record.flowPaths = std::move(newPaths);
				return true;
			}

			// The hand rivers as they are on the pages: the open editor's (saved or not), else the .hflow file.
			std::vector<terrain_io::water_flow::FlowPath> paths;
			terrain_io::water_flow::FlowSettings settings;
			if (!editor.GetHandFlowPaths(paths, settings))
			{
				FlowPathsFile handRivers;
				String error;
				if (!ReadFlowPathsFile(editor.GetFlowPathsFileName(), handRivers, error))
				{
					warnings.push_back("the river flow was not updated: " + error);
					WLOG("PCG: graph instance '" << record.name << "' (" << FormatId(record.id) << "): " << warnings.back());
					return false;
				}

				paths.clear();
				for (const auto& river : handRivers.rivers)
				{
					if (river.second.points.size() >= 2)
					{
						paths.push_back(terrain_io::water_flow::FromSpline(river.second));
					}
				}
				settings = handRivers.settings;
			}

			const std::vector<terrain_io::water_flow::FlowPath> generated = controls.CollectGeneratedFlowPaths(record.id, newPaths);
			paths.insert(paths.end(), generated.begin(), generated.end());

			// Same settings for both sets: Water mode applied the pages with them. No material: ApplyWaterFlow would give it
			// to every page with water near any path (hand rivers and pages outside the region included) and nothing would
			// take it back. The instance's own water gets its material from the terrain write (content.waterMaterial, the
			// River op's material), which the contribution undo takes back with the water.
			const terrain::Terrain::WaterFlowResult result = terrain->ApplyWaterFlow(paths, settings, record.flowPaths,
				settings, String());
			ILOG("PCG: graph instance '" << record.name << "' (" << FormatId(record.id) << "): river flow written to "
				<< result.pagesWritten << " page(s), cleared on " << result.pagesCleared);
			record.flowPaths = std::move(newPaths);
			return true;
		}
	}

	const pcg::SectionAsset* SectionAssetCache::Get(const String& path, String& error)
	{
		auto it = m_entries.find(path);
		if (it == m_entries.end())
		{
			Entry entry;
			// Read before the files: a write during the load shows up as a changed time later.
			entry.assetTime = AssetRegistry::GetLastWriteTime(path);
			entry.worldTime = AssetRegistry::GetLastWriteTime(GetSectionWorldPathForAsset(path));
			entry.asset = std::make_unique<pcg::SectionAsset>();
			String staleWarning;
			if (!LoadSectionAsset(path, *entry.asset, entry.error))
			{
				entry.asset.reset();
			}
			else if (IsSectionAssetStale(path, *entry.asset, staleWarning))
			{
				entry.error = "it is stale, its section world '" + GetSectionWorldPathForAsset(path)
					+ "' changed since the last bake. Bake the section again.";
				entry.asset.reset();
			}
			it = m_entries.emplace(path, std::move(entry)).first;
		}

		error = it->second.error;
		return it->second.asset.get();
	}

	const pcg::SectionAsset* SectionAssetCache::Find(const String& path, String& error, bool& cached) const
	{
		const auto it = m_entries.find(path);
		cached = it != m_entries.end();
		if (!cached)
		{
			error.clear();
			return nullptr;
		}

		error = it->second.error;
		return it->second.asset.get();
	}

	pcg::graph::SectionAssetProvider SectionAssetCache::MakeProvider()
	{
		return [this](const String& path, String& error)
		{
			return Get(path, error);
		};
	}

	bool SectionAssetCache::DropChanged()
	{
		bool dropped = false;
		for (auto it = m_entries.begin(); it != m_entries.end();)
		{
			if (AssetRegistry::GetLastWriteTime(it->first) != it->second.assetTime ||
				AssetRegistry::GetLastWriteTime(GetSectionWorldPathForAsset(it->first)) != it->second.worldTime)
			{
				it = m_entries.erase(it);
				dropped = true;
			}
			else
			{
				++it;
			}
		}
		if (dropped)
		{
			++m_revision;
		}
		return dropped;
	}

	void SectionAssetCache::Clear()
	{
		if (!m_entries.empty())
		{
			m_entries.clear();
			++m_revision;
		}
	}

	uint64 SectionAssetCache::GetRevision() const
	{
		return m_revision;
	}

	pcg::graph::Bounds2D GetPlacedControlBounds(const pcg::graph::ControlInstance& instance, const pcg::graph::ControlType* type)
	{
		if (!type)
		{
			// Unknown shape: spline controls keep their points, volumes and points have none.
			return instance.spline.points.empty() ? GetVolumeFootprint(instance) : pcg::graph::SplineBounds(instance.spline);
		}

		pcg::graph::Bounds2D bounds;
		switch (type->shape)
		{
		case pcg::graph::ControlShape::ClosedLoop:
		case pcg::graph::ControlShape::OpenSpline:
			return pcg::graph::SplineBounds(instance.spline);

		case pcg::graph::ControlShape::Volume:
			return GetVolumeFootprint(instance);

		case pcg::graph::ControlShape::Point:
			bounds.Add(instance.position.x, instance.position.z);
			return bounds;
		}
		return bounds;
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

		controls.ResolvePlacedControls(out.controls, out.warnings, &out.unresolved);

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

	std::unique_ptr<BaseTerrainSampler> MakeBaseTerrainSampler(IWorldEditor& editor, const pcg::GraphInstanceRecord& record,
		const EditorTerrainSampler& live)
	{
		terrain::Terrain* terrain = editor.GetTerrain();
		if (!terrain || record.content.terrain.IsEmpty())
		{
			return nullptr;
		}

		return std::make_unique<BaseTerrainSampler>(live, *terrain, record.content.terrain);
	}

	bool UseBaseTerrain(IWorldEditor& editor, const pcg::GraphInstanceRecord& record, GraphInstanceContext& context)
	{
		if (!context.sampler)
		{
			return false;
		}

		context.baseSampler = MakeBaseTerrainSampler(editor, record, *context.sampler);
		if (!context.baseSampler)
		{
			return false;
		}

		context.context.terrain = context.baseSampler.get();
		if (context.baseSampler->HasMaterialChanges())
		{
			context.warnings.push_back(BaseTerrainMaterialWarning);
		}
		return true;
	}

	bool PrepareNodeTerrainOps(const pcg::graph::Graph& graph, const pcg::graph::EvalResult& result, const uint32 nodeId,
		const pcg::GraphInstanceRecord& record, const std::function<float(float, float)>& ground, SectionAssetCache& sections,
		pcg::graph::PreparedTerrain& out, String& error, const std::vector<PathSpline>& neighbours)
	{
		std::vector<pcg::graph::TerrainOpInput> ops = pcg::graph::CollectTerrainOps(graph, result);
		ops.erase(std::remove_if(ops.begin(), ops.end(), [nodeId](const pcg::graph::TerrainOpInput& op)
		{
			return op.op.order != nodeId;
		}), ops.end());

		return out.Prepare(ops, ground, sections.MakeProvider(), record.area, record.borderFade, error, neighbours);
	}

	bool EvaluateGraphInstance(IWorldEditor& editor, const pcg::GraphInstanceRecord& record, ProceduralEditMode& controls,
		GraphInstanceEvaluation& out, String& error, const bool baseTerrain)
	{
		if (!LoadGraphAsset(record.graphPath, out.graph, error))
		{
			return false;
		}

		return EvaluateLoadedGraph(editor, record, controls, out, error, baseTerrain);
	}

	bool GraphHasTerrainOps(const pcg::graph::Graph& graph)
	{
		const pcg::graph::NodeRegistry& registry = pcg::graph::NodeRegistry::Default();
		for (const pcg::graph::NodeInstance& node : graph.nodes)
		{
			const pcg::graph::NodeTypeDef* def = registry.Find(node.type);
			if (def == nullptr)
			{
				continue;
			}
			for (const pcg::graph::PinDef& pin : def->outputs)
			{
				if (pin.name == "Terrain")
				{
					return true;
				}
			}
		}
		return false;
	}

	void CollectSeamNeighbours(const pcg::GraphInstanceRecord& record, const std::vector<const pcg::GraphInstanceRecord*>& instances,
		std::vector<PathSpline>& areas, std::vector<String>* names)
	{
		areas.clear();
		if (names != nullptr)
		{
			names->clear();
		}

		std::vector<const pcg::GraphInstanceRecord*> sorted;
		for (const pcg::GraphInstanceRecord* other : instances)
		{
			if (other != nullptr && other->id != record.id)
			{
				sorted.push_back(other);
			}
		}
		std::sort(sorted.begin(), sorted.end(), [](const pcg::GraphInstanceRecord* a, const pcg::GraphInstanceRecord* b)
		{
			return a->id < b->id;
		});

		for (const pcg::GraphInstanceRecord* other : sorted)
		{
			// A terrain writer: it has written terrain, or its graph has terrain ops (phase 3a rule R11).
			bool writer = !other->content.terrain.IsEmpty();
			if (!writer)
			{
				pcg::graph::Graph graph;
				String ignored;
				writer = LoadGraphAsset(other->graphPath, graph, ignored) && GraphHasTerrainOps(graph);
			}
			if (writer)
			{
				areas.push_back(other->area);
				if (names != nullptr)
				{
					names->push_back(other->name);
				}
			}
		}
	}

	struct GraphRebuildJob::Impl
	{
		Clock::time_point start = Clock::now();
		terrain::Terrain* terrain = nullptr;

		/// The record as it was when the job started (its content is the session's business).
		pcg::GraphInstanceRecord record;
		pcg::graph::Graph graph;

		/// Copies of the placed controls the evaluation reads, and the resolved controls pointing at them.
		std::deque<pcg::graph::ControlType> controlTypes;
		std::deque<pcg::graph::ControlInstance> controlInstances;
		std::vector<pcg::graph::ResolvedControl> controls;
		std::vector<String> contextWarnings;
		std::vector<UnresolvedPlacedControl> unresolved;

		/// The terrain the evaluation reads (warmed tile cache, cache only, the revision fixed at the start).
		std::unique_ptr<EditorTerrainSampler> sampler;
		pcg::graph::EvalContext context;

		std::vector<PathSpline> neighbourAreas;
		std::vector<String> neighbourNames;
		std::shared_ptr<SectionAssetCache> sections;

		pcg::WorldRect areaRect{};
		ContentRebuildSession session;
		uint64 terrainRevision = 0;

		/// Read-only terrain access of Run (the session's access writes, and is main thread only).
		std::unique_ptr<EditorTerrainAccess> readAccess;

		/// Texture key to tile layer matching of the terrain samples: the warmed main cache, then one copy per further
		/// worker thread with its own notes.
		std::unique_ptr<EditorTextureLayers> layers;
		pcg::graph::TextureLayerLookup lookup;
		pcg::graph::TerrainEvaluateNotes notes;
		std::vector<std::unique_ptr<EditorTextureLayers>> workerLayers;
		std::vector<pcg::graph::TextureLayerLookup> workerLookups;
		std::vector<pcg::graph::TerrainEvaluateNotes> workerNotes;

		// Results of Run.
		bool ran = false;
		String runError;
		std::set<String> missingSections;
		GraphInstanceEvaluation evaluation;
		pcg::graph::PreparedTerrain prepared;
		pcg::TerrainSamples samples;
		std::vector<float> spawnOffsets;

		std::atomic<bool> canceled{ false };
	};

	GraphRebuildJob::GraphRebuildJob()
		: m_impl(std::make_unique<Impl>())
	{
	}

	GraphRebuildJob::~GraphRebuildJob()
	{
		if (m_impl && m_impl->session.active)
		{
			ELOG("PCG: a rebuild of '" << m_impl->record.name << "' was dropped without Commit or Abort; its old terrain contribution is not on the terrain");
		}
	}

	uint64 GraphRebuildJob::GetInstanceId() const
	{
		return m_impl->record.id;
	}

	double GraphRebuildJob::GetElapsedSeconds() const
	{
		return MillisecondsSince(m_impl->start) / 1000.0;
	}

	void GraphRebuildJob::Cancel()
	{
		m_impl->canceled = true;
	}

	bool GraphRebuildJob::IsCanceled() const
	{
		return m_impl->canceled;
	}

	bool GraphRebuildJob::NeedsSectionAssets() const
	{
		return !m_impl->missingSections.empty();
	}

	void GraphRebuildJob::LoadSectionAssets()
	{
		for (const String& path : m_impl->missingSections)
		{
			String ignored;
			m_impl->sections->Get(path, ignored);
		}
		m_impl->missingSections.clear();
	}

	std::unique_ptr<GraphRebuildJob> GraphRebuildJob::Start(IWorldEditor& editor, pcg::GraphInstanceRecord& record,
		const std::vector<const pcg::GraphInstanceRecord*>& otherInstances, ProceduralEditMode& controls, String& error)
	{
		if (!CheckArea(record.area, error))
		{
			return nullptr;
		}

		terrain::Terrain* terrain = editor.GetTerrain();
		if (!terrain || !editor.HasTerrain())
		{
			error = "the world has no terrain";
			return nullptr;
		}

		// An area wholly outside the terrain would clamp to an inverted influence.
		const pcg::WorldRect areaRect = ClampToTerrain(GetAreaRect(record.area), *terrain);
		if (areaRect.minX > areaRect.maxX || areaRect.minZ > areaRect.maxZ)
		{
			error = "the area lies outside the terrain";
			return nullptr;
		}

		for (const pcg::GraphInstanceRecord* other : otherInstances)
		{
			if (other && other->id != record.id && pcg::AreasOverlap(record.area, other->area))
			{
				error = "its area overlaps the area of graph instance '" + other->name + "' (" + FormatId(other->id) + ")";
				return nullptr;
			}
		}

		std::unique_ptr<GraphRebuildJob> job(new GraphRebuildJob());
		Impl& impl = *job->m_impl;
		impl.terrain = terrain;
		impl.record = record;
		impl.record.content = pcg::OwnedContent();
		impl.areaRect = areaRect;
		if (!LoadGraphAsset(record.graphPath, impl.graph, error))
		{
			return nullptr;
		}

		// Resolves the controls and loads the pages the evaluation reads.
		GraphInstanceContext live;
		if (!BuildGraphInstanceContext(editor, record, controls, live, error))
		{
			return nullptr;
		}

		// The resolved controls point into the procedural mode, which keeps changing while the job runs: copy them.
		std::map<const pcg::graph::ControlType*, const pcg::graph::ControlType*> typeCopies;
		for (const pcg::graph::ResolvedControl& resolved : live.controls)
		{
			pcg::graph::ResolvedControl copy = resolved;
			if (resolved.type)
			{
				auto it = typeCopies.find(resolved.type);
				if (it == typeCopies.end())
				{
					impl.controlTypes.push_back(*resolved.type);
					it = typeCopies.emplace(resolved.type, &impl.controlTypes.back()).first;
				}
				copy.type = it->second;
			}
			if (resolved.instance)
			{
				impl.controlInstances.push_back(*resolved.instance);
				copy.instance = &impl.controlInstances.back();
			}
			impl.controls.push_back(std::move(copy));
		}
		impl.contextWarnings = live.warnings;
		impl.unresolved = live.unresolved;

		if (GraphHasTerrainOps(impl.graph))
		{
			CollectSeamNeighbours(record, otherInstances, impl.neighbourAreas, &impl.neighbourNames);
		}
		// The world's rebuild cache: kept loaded between rebuilds, assets changed on disk read again.
		impl.sections = controls.GetRebuildSectionAssets();
		impl.sections->DropChanged();

		// Takes the old contribution out of the terrain data; the world keeps showing it until Commit or Abort.
		if (!BeginContentRebuild(editor, areaRect, record.content, impl.session, error))
		{
			return nullptr;
		}
		impl.terrainRevision = editor.GetTerrainRevision();

		// Everything Run reads besides the terrain heights is read here, on the base terrain.
		const uint64 revision = impl.terrainRevision;
		impl.sampler = std::make_unique<EditorTerrainSampler>(*terrain, [revision]() { return revision; });
		pcg::graph::Bounds2D bounds = pcg::graph::SplineBounds(record.area);
		for (const pcg::graph::ResolvedControl& control : impl.controls)
		{
			const pcg::graph::Bounds2D controlBounds = GetControlBounds(control);
			if (controlBounds.valid && pcg::graph::SaneBounds(controlBounds) && controlBounds.Intersects(pcg::graph::SplineBounds(record.area)))
			{
				bounds.Add(controlBounds.minX, controlBounds.minZ);
				bounds.Add(controlBounds.maxX, controlBounds.maxZ);
			}
		}
		const float margin = static_cast<float>(terrain::constants::PageSize / 8.0);
		impl.sampler->PrewarmTiles({ bounds.minX - margin, bounds.minZ - margin, bounds.maxX + margin, bounds.maxZ + margin });
		impl.sampler->SetCacheOnly(true);

		impl.context = pcg::graph::EvalContext();
		impl.context.instanceSeed = record.seed;
		impl.context.region = record.area;
		impl.context.parameters = record.parameterOverrides;
		impl.context.controls = &impl.controls;
		// 0: Evaluate hashes the controls itself.
		impl.context.controlsRevision = 0;
		impl.context.terrain = impl.sampler.get();
		impl.context.pointLimit = record.pointLimit;

		impl.readAccess = std::make_unique<EditorTerrainAccess>(*terrain);
		// Warmed on the main thread; Run only uses copies, which never read the terrain.
		EditorTextureLayers warm(*terrain);
		warm.Prewarm(areaRect);
		impl.layers = warm.CopyWarmCache();
		impl.lookup = impl.layers->MakeLookup();
		const size_t workers = pcg::HardwareWorkers();
		impl.workerNotes.assign(workers - 1, pcg::graph::TerrainEvaluateNotes{});
		for (size_t i = 1; i < workers; ++i)
		{
			impl.workerLayers.push_back(warm.CopyWarmCache());
			impl.workerLookups.push_back(impl.workerLayers.back()->MakeLookup());
		}
		return job;
	}

	void GraphRebuildJob::Run()
	{
		Impl& impl = *m_impl;
		impl.ran = false;
		impl.runError.clear();
		impl.missingSections.clear();
		impl.notes = pcg::graph::TerrainEvaluateNotes{};
		for (pcg::graph::TerrainEvaluateNotes& notes : impl.workerNotes)
		{
			notes = pcg::graph::TerrainEvaluateNotes{};
		}
		if (impl.canceled)
		{
			return;
		}

		// The graph on the base terrain (EvaluateLoadedGraph with the copied context).
		GraphInstanceEvaluation& evaluation = impl.evaluation;
		evaluation = GraphInstanceEvaluation();
		evaluation.graph = impl.graph;
		evaluation.warnings = impl.contextWarnings;
		evaluation.unresolved = impl.unresolved;
		pcg::graph::MergeParameters(evaluation.graph.parameters, impl.record.parameterOverrides, &evaluation.warnings);
		evaluation.result = pcg::graph::Evaluate(evaluation.graph, pcg::graph::NodeRegistry::Default(), impl.context, nullptr);
		if (!evaluation.result.issues.empty())
		{
			// Commit refuses with the issues.
			impl.ran = true;
			return;
		}
		evaluation.collected = pcg::graph::CollectPlacements(evaluation.graph, evaluation.result, impl.record.area);
		evaluation.warnings.insert(evaluation.warnings.end(), evaluation.collected.warnings.begin(), evaluation.collected.warnings.end());
		if (impl.canceled)
		{
			return;
		}

		// Terrain ops on the base ground. Section assets come from the cache only; missing ones are loaded on the main
		// thread (LoadSectionAssets) and the job runs again.
		terrain::Terrain* terrain = impl.terrain;
		const GroundFunction ground = [terrain](const float x, const float z)
		{
			return terrain->GetSmoothHeightAt(x, z);
		};
		const pcg::graph::SectionAssetProvider provider = [&impl](const String& path, String& assetError) -> const pcg::SectionAsset*
		{
			bool cached = false;
			const pcg::SectionAsset* asset = impl.sections->Find(path, assetError, cached);
			if (!cached)
			{
				impl.missingSections.insert(path);
				assetError = "not loaded yet";
			}
			return asset;
		};
		String prepareError;
		if (!impl.prepared.Prepare(pcg::graph::CollectTerrainOps(evaluation.graph, evaluation.result), ground, provider, impl.record.area,
			impl.record.borderFade, prepareError, impl.neighbourAreas))
		{
			if (impl.missingSections.empty())
			{
				impl.runError = "the terrain ops could not be prepared: " + prepareError;
				impl.ran = true;
			}
			return;
		}
		if (impl.canceled)
		{
			return;
		}

		// Every terrain sample, on one target per hardware thread.
		std::vector<pcg::ChannelTerrainTargetFunction> targets;
		// Once canceled the targets return nothing, so the computation ends quickly (its result is not used).
		targets.push_back([&impl](const float x, const float z, const float baseHeight, const pcg::terrain_channel::Type channels)
		{
			return impl.canceled ? pcg::TerrainTarget{} : impl.prepared.Evaluate(x, z, baseHeight, impl.lookup, &impl.notes, channels);
		});
		for (size_t i = 0; i < impl.workerLookups.size(); ++i)
		{
			targets.push_back([&impl, i](const float x, const float z, const float baseHeight, const pcg::terrain_channel::Type channels)
			{
				return impl.canceled ? pcg::TerrainTarget{} : impl.prepared.Evaluate(x, z, baseHeight, impl.workerLookups[i], &impl.workerNotes[i], channels);
			});
		}
		pcg::ComputeTerrainSamples(*impl.readAccess, impl.areaRect, targets, impl.samples);

		// Spawns keep their height above the base ground.
		impl.spawnOffsets.clear();
		for (const pcg::Placement& placement : evaluation.collected.placements)
		{
			impl.spawnOffsets.push_back(placement.position.y - ground(placement.position.x, placement.position.z));
		}
		impl.ran = !impl.canceled;
	}

	void GraphRebuildJob::Abort(pcg::GraphInstanceRecord& record)
	{
		AbortContentRebuild(m_impl->session, record.content);
	}

	GeneratorBuildReport GraphRebuildJob::Commit(IWorldEditor& editor, pcg::GraphInstanceRecord& record,
		const std::vector<const pcg::GraphInstanceRecord*>& otherInstances, ProceduralEditMode& controls,
		const std::function<bool(const pcg::WorldRect& areaRect, String& error)>& checkTerrainOverlap)
	{
		Impl& impl = *m_impl;
		GeneratorBuildReport report;
		const auto refuse = [this, &impl, &report, &record](const String& reason)
		{
			Abort(record);
			report.error = "Rebuild of '" + impl.record.name + "' refused, the world was not changed: " + reason;
			report.milliseconds = MillisecondsSince(impl.start);
			ELOG("PCG: " << report.error);
			return report;
		};

		if (impl.canceled)
		{
			return refuse("it was canceled");
		}

		std::vector<String> warnings;
		for (int attempt = 0;; ++attempt)
		{
			if (editor.GetTerrainRevision() != impl.terrainRevision)
			{
				return refuse("the terrain changed while it was computed; rebuild again");
			}
			if (!impl.ran)
			{
				return refuse(impl.runError.empty() ? "it was not computed" : impl.runError);
			}

			String error;
			warnings.clear();
			if (!CheckEvaluation(impl.evaluation, impl.record.area, warnings, error))
			{
				return refuse(error);
			}
			if (!impl.runError.empty())
			{
				return refuse(impl.runError);
			}

			// Other instances may have moved since the start.
			for (const pcg::GraphInstanceRecord* other : otherInstances)
			{
				if (other && other->id != record.id && pcg::AreasOverlap(impl.record.area, other->area))
				{
					return refuse("its area overlaps the area of graph instance '" + other->name + "' (" + FormatId(other->id) + ")");
				}
			}
			if (!impl.prepared.IsEmpty() && checkTerrainOverlap && !checkTerrainOverlap(impl.areaRect, error))
			{
				return refuse(error);
			}

			// Ocean pages only become terrain where an op reaches; doing so changes the base, so the job runs again on it.
			const std::vector<pcg::WorldRect> writeRects = { impl.prepared.IsEmpty() ? pcg::WorldRect{ 1.0f, 1.0f, 0.0f, 0.0f } : impl.prepared.GetInfluence() };
			if (attempt > 0 || !NeedsOceanMaterialization(*impl.terrain, impl.areaRect, writeRects))
			{
				break;
			}
			MaterializeOceanPagesForWrites(*impl.terrain, impl.areaRect, writeRects);
			impl.terrainRevision = editor.GetTerrainRevision();
			Run();
			while (NeedsSectionAssets())
			{
				LoadSectionAssets();
				Run();
			}
		}

		const std::vector<pcg::Placement>& spawns = impl.evaluation.collected.placements;
		size_t sectionPlacementCount = 0;
		const auto placements = [&impl, &spawns, &warnings, &sectionPlacementCount](const GroundFunction& ground, const GroundFunction&, std::vector<pcg::Placement>& out)
		{
			// Spawns keep their height above the ground, now the final one.
			out.clear();
			std::set<uint64> keys;
			for (size_t i = 0; i < spawns.size(); ++i)
			{
				pcg::Placement placement = spawns[i];
				placement.position.y = ground(placement.position.x, placement.position.z) + impl.spawnOffsets[i];
				keys.insert(placement.key);
				out.push_back(std::move(placement));
			}

			std::vector<pcg::Placement> sectionPlacements;
			impl.prepared.BuildPlacements(ground, sectionPlacements, &warnings);
			for (pcg::Placement& placement : sectionPlacements)
			{
				placement.key = pcg::Hash(placement.key, 0, 0, SectionPlacementKeySalt);
				if (!keys.insert(placement.key).second)
				{
					warnings.push_back("a section placement (" + placement.asset + ") was dropped: its key collides with another placement");
					continue;
				}
				out.push_back(std::move(placement));
				++sectionPlacementCount;
			}

			std::sort(out.begin(), out.end(), [](const pcg::Placement& a, const pcg::Placement& b)
			{
				return a.key < b.key;
			});
		};

		const std::vector<String>& terrainWarnings = impl.prepared.GetWarnings();
		warnings.insert(warnings.end(), terrainWarnings.begin(), terrainWarnings.end());

		report = FinishContentRebuild(editor, impl.session, impl.samples, String(), impl.prepared.GetWaterMaterial(), placements,
			record.content, record.id, GraphInstanceLabel);
		for (const pcg::graph::TerrainEvaluateNotes& notes : impl.workerNotes)
		{
			impl.notes.missingLayerOps.insert(notes.missingLayerOps.begin(), notes.missingLayerOps.end());
			impl.notes.missingSectionLayers.insert(notes.missingSectionLayers.begin(), notes.missingSectionLayers.end());
		}
		report.warnings.insert(report.warnings.end(), warnings.begin(), warnings.end());
		impl.prepared.DescribeNotes(impl.notes, report.warnings);
		if (!report.ok)
		{
			report.error = "Rebuild of '" + impl.record.name + "' failed: " + report.error;
			report.milliseconds = MillisecondsSince(impl.start);
			ELOG("PCG: " << report.error);
			return report;
		}

		// The terrain write succeeded: the river paths of the terrain that was actually applied join the world's flow.
		std::vector<terrain_io::water_flow::FlowPath> flowPaths;
		impl.prepared.GetFlowPaths(flowPaths);
		ApplyInstanceFlow(editor, record, controls, std::move(flowPaths), report.warnings);

		// The seam set decides which edges do not fade; a neighbour that was created or deleted since the last build
		// leaves its side of the seam as it was until it is rebuilt too.
		const uint64 seamHash = impl.prepared.GetSeamHash();
		if (seamHash != record.seamHash)
		{
			String touching;
			for (const size_t index : impl.prepared.GetSeamNeighbours())
			{
				if (index < impl.neighbourNames.size())
				{
					touching += (touching.empty() ? "'" : ", '") + impl.neighbourNames[index] + "'";
				}
			}
			report.warnings.push_back(touching.empty()
				? "seams changed since the last build of '" + record.name + "'; rebuild the touching instances too"
				: "seams changed since the last build of " + touching + "; rebuild " + (impl.prepared.GetSeamNeighbours().size() > 1 ? "them" : "it") + " too");
			WLOG("PCG: graph instance '" << record.name << "' (" << FormatId(record.id) << "): " << report.warnings.back());
			record.seamHash = seamHash;
		}
		report.milliseconds = MillisecondsSince(impl.start);

		ILOG("PCG: graph instance '" << record.name << "' (" << FormatId(record.id) << "): " << spawns.size()
			<< " spawn placement(s) inside the area, " << impl.evaluation.collected.clipped << " clipped, " << sectionPlacementCount
			<< " section placement(s), " << report.terrainSamples << " terrain sample(s) written");
		return report;
	}

	GeneratorBuildReport RebuildGraphInstance(IWorldEditor& editor, pcg::GraphInstanceRecord& record,
		const std::vector<const pcg::GraphInstanceRecord*>& otherInstances, ProceduralEditMode& controls,
		const std::function<bool(const pcg::WorldRect& areaRect, String& error)>& checkTerrainOverlap)
	{
		// The steps of a background rebuild, on this thread.
		String error;
		const std::unique_ptr<GraphRebuildJob> job = GraphRebuildJob::Start(editor, record, otherInstances, controls, error);
		if (!job)
		{
			GeneratorBuildReport report;
			report.error = "Rebuild of '" + record.name + "' refused, the world was not changed: " + error;
			ELOG("PCG: " << report.error);
			return report;
		}

		// Each run stops at the first section asset that is not loaded yet; every round loads at least one.
		job->Run();
		while (job->NeedsSectionAssets())
		{
			job->LoadSectionAssets();
			job->Run();
		}
		return job->Commit(editor, record, otherInstances, controls, checkTerrainOverlap);
	}

	GeneratorBuildReport DeleteGraphInstanceContent(IWorldEditor& editor, pcg::GraphInstanceRecord& record, const ProceduralEditMode& controls)
	{
		GeneratorBuildReport report = RemoveGeneratedContent(editor, record.content);
		if (report.ok)
		{
			ApplyInstanceFlow(editor, record, controls, {}, report.warnings);
			record.seamHash = 0;
		}
		return report;
	}
}
