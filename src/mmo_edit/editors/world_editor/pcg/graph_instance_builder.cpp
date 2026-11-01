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
#include "pcg/pcg_random.h"
#include "terrain/constants.h"
#include "terrain/terrain.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <map>
#include <memory>
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

		/// What content.prepare builds on the base terrain, read by the terrain and placement steps.
		struct GraphBuildState
		{
			/// The terrain ops prepared on the base.
			pcg::graph::PreparedTerrain terrain;

			/// The spawned placements (inside the area), as evaluated on the base.
			std::vector<pcg::Placement> spawns;

			/// Per spawn: its height above the base ground the graph saw.
			std::vector<float> spawnOffsets;

			/// Spawned points dropped for lying outside the area.
			size_t clipped = 0;

			/// Section placements added by the last placement step.
			size_t sectionPlacements = 0;

			/// True if content.prepare took over the dry run instead of evaluating again.
			bool reusedDryRun = false;

			/// Paint layers the terrain step had to skip.
			pcg::graph::TerrainEvaluateNotes notes;

			/// Texture key to tile layer matching of the terrain step (lookup calls layers).
			std::unique_ptr<EditorTextureLayers> layers;
			pcg::graph::TextureLayerLookup lookup;

			/// Warnings of the evaluation on the base, the terrain preparation and the placement step.
			std::vector<String> warnings;
		};

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
			const GroundFunction& ground, SectionAssetCache& sections, pcg::graph::PreparedTerrain& out, String& error)
		{
			String prepareError;
			if (!out.Prepare(pcg::graph::CollectTerrainOps(evaluation.graph, evaluation.result), ground, sections.MakeProvider(),
				record.area, record.borderFade, prepareError))
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

		/// Makes the instance's river paths newPaths: recomputes the world's river flow from the hand rivers (the world's
		/// .hflow, in file order) followed by every graph instance's paths (this one's replaced by newPaths), with this
		/// instance's old paths as the previous set, then stores newPaths in the record. Bit-identical paths change
		/// nothing. Call it only after the terrain write (or removal) succeeded.
		/// @return False (with a warning, the record and the flow left as they were) if the .hflow file cannot be read.
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

			FlowPathsFile handRivers;
			String error;
			if (!ReadFlowPathsFile(editor.GetFlowPathsFileName(), handRivers, error))
			{
				warnings.push_back("the river flow was not updated: " + error);
				WLOG("PCG: graph instance '" << record.name << "' (" << FormatId(record.id) << "): " << warnings.back());
				return false;
			}

			std::vector<terrain_io::water_flow::FlowPath> paths;
			for (const auto& river : handRivers.rivers)
			{
				if (river.second.points.size() >= 2)
				{
					paths.push_back(terrain_io::water_flow::FromSpline(river.second));
				}
			}
			const std::vector<terrain_io::water_flow::FlowPath> generated = controls.CollectGeneratedFlowPaths(record.id, newPaths);
			paths.insert(paths.end(), generated.begin(), generated.end());

			const terrain::Terrain::WaterFlowResult result = terrain->ApplyWaterFlow(paths, handRivers.settings, record.flowPaths,
				handRivers.settings, newPaths.empty() ? String() : String(RiverWaterMaterial));
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
		pcg::graph::PreparedTerrain& out, String& error)
	{
		std::vector<pcg::graph::TerrainOpInput> ops = pcg::graph::CollectTerrainOps(graph, result);
		ops.erase(std::remove_if(ops.begin(), ops.end(), [nodeId](const pcg::graph::TerrainOpInput& op)
		{
			return op.op.order != nodeId;
		}), ops.end());

		return out.Prepare(ops, ground, sections.MakeProvider(), record.area, record.borderFade, error);
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

	GeneratorBuildReport RebuildGraphInstance(IWorldEditor& editor, pcg::GraphInstanceRecord& record,
		const std::vector<const pcg::GraphInstanceRecord*>& otherInstances, ProceduralEditMode& controls,
		const std::function<bool(const pcg::WorldRect& areaRect, String& error)>& checkTerrainOverlap)
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

		// An area wholly outside the terrain would clamp to an inverted influence.
		if (const terrain::Terrain* terrain = editor.GetTerrain())
		{
			const pcg::WorldRect clamped = ClampToTerrain(GetAreaRect(record.area), *terrain);
			if (clamped.minX > clamped.maxX || clamped.minZ > clamped.maxZ)
			{
				return refuse("the area lies outside the terrain");
			}
		}

		for (const pcg::GraphInstanceRecord* other : otherInstances)
		{
			if (other && other->id != record.id && pcg::AreasOverlap(record.area, other->area))
			{
				return refuse("its area overlaps the area of graph instance '" + other->name + "' (" + FormatId(other->id) + ")");
			}
		}

		// Every refusal is decided here, before ApplyGeneratedContent changes anything: a dry run on the live
		// terrain (which still carries the previous contribution). Neither the checks nor the section assets, the
		// op geometry or the influence depend on the heights; content.prepare repeats the run on the base terrain
		// and fails only on what this run cannot see (ApplyGeneratedContent then restores the old contribution).
		GraphInstanceEvaluation dryRun;
		if (!EvaluateGraphInstance(editor, record, controls, dryRun, error))
		{
			return refuse(error);
		}

		// The warnings come from the run on the base terrain (content.prepare), which takes these over only when it
		// reuses the dry run.
		std::vector<String> dryWarnings;
		if (!CheckEvaluation(dryRun, record.area, dryWarnings, error))
		{
			return refuse(error);
		}

		// EvaluateGraphInstance built the context, so the terrain exists.
		terrain::Terrain* terrain = editor.GetTerrain();
		const GroundFunction liveGround = [terrain](const float x, const float z)
		{
			return terrain->GetSmoothHeightAt(x, z);
		};

		const auto sections = std::make_shared<SectionAssetCache>();
		pcg::graph::PreparedTerrain dryTerrain;
		if (!PrepareTerrainOps(dryRun, record, liveGround, *sections, dryTerrain, error))
		{
			return refuse(error);
		}

		// Clamped to the terrain: the bounds include tangent handles, which may reach past its edge (nothing is
		// written there anyway) while ApplyGeneratedContent needs an influence inside the terrain.
		const pcg::WorldRect areaRect = ClampToTerrain(GetAreaRect(record.area), *terrain);
		if (!dryTerrain.IsEmpty() && checkTerrainOverlap && !checkTerrainOverlap(areaRect, error))
		{
			return refuse(error);
		}

		const auto state = std::make_shared<GraphBuildState>();
		state->layers = std::make_unique<EditorTextureLayers>(*terrain);
		state->lookup = state->layers->MakeLookup();

		GeneratedContent content;
		// Writes are clipped to the area. The terrain step is always set, so the previous contribution is undone on
		// every rebuild and a graph without terrain ops writes nothing.
		content.influence = areaRect;
		// Ocean pages only become terrain where an op reaches (an empty rect: nowhere).
		content.writeRects = { dryTerrain.IsEmpty() ? pcg::WorldRect{ 1.0f, 1.0f, 0.0f, 0.0f } : dryTerrain.GetInfluence() };
		content.waterMaterial = dryTerrain.GetWaterMaterial();

		const pcg::graph::Graph& graph = dryRun.graph;
		const bool dryEmpty = dryTerrain.IsEmpty();
		const pcg::WorldRect dryInfluence = dryTerrain.GetInfluence();
		const String dryWaterMaterial = dryTerrain.GetWaterMaterial();

		// Without a recorded contribution the live terrain already is the base: unless the terrain changed since the dry
		// run (an ocean page materialised for the writes), prepare takes over the dry run instead of evaluating again.
		const bool liveIsBase = record.content.terrain.IsEmpty();
		const uint64 dryTerrainRevision = editor.GetTerrainRevision();
		content.prepare = [&editor, &record, &controls, &graph, &dryRun, &dryWarnings, &dryTerrain, liveIsBase, dryTerrainRevision,
			sections, state, dryEmpty, dryInfluence, dryWaterMaterial](const GroundFunction& ground, String& prepareError)
		{
			// The previous contribution is undone: the graph sees the base terrain.
			GraphInstanceEvaluation base;
			std::vector<String> warnings;
			if (liveIsBase && editor.GetTerrainRevision() == dryTerrainRevision)
			{
				// Same terrain, same ground (GetSmoothHeightAt): the dry run's evaluation and prepared ops are what this
				// run would produce. The dry run is not used after this step.
				base = std::move(dryRun);
				warnings = std::move(dryWarnings);
				state->terrain = std::move(dryTerrain);
				state->reusedDryRun = true;
			}
			else
			{
				base.graph = graph;
				if (!EvaluateLoadedGraph(editor, record, controls, base, prepareError) ||
					!CheckEvaluation(base, record.area, warnings, prepareError) ||
					!PrepareTerrainOps(base, record, ground, *sections, state->terrain, prepareError))
				{
					return false;
				}
			}

			// writeRects and waterMaterial were fixed from the dry run before the undo (ApplyGeneratedContent reads them
			// first). Op geometry and materials do not depend on the heights, so they must not differ; if they do
			// anyway, ocean pages outside the dry influence would silently drop writes or the water would get the
			// wrong material, so the build fails (and the old contribution is put back).
			const pcg::WorldRect influence = state->terrain.GetInfluence();
			const bool contained = state->terrain.IsEmpty() || (!dryEmpty && influence.minX >= dryInfluence.minX &&
				influence.minZ >= dryInfluence.minZ && influence.maxX <= dryInfluence.maxX && influence.maxZ <= dryInfluence.maxZ);
			if (!contained)
			{
				prepareError = "the terrain ops reach further on the base terrain than on the live terrain; rebuild again";
				return false;
			}
			if (state->terrain.GetWaterMaterial() != dryWaterMaterial)
			{
				prepareError = "the water material of the terrain ops differs between the live and the base terrain ('"
					+ dryWaterMaterial + "' vs '" + state->terrain.GetWaterMaterial() + "'); rebuild again";
				return false;
			}

			state->spawns = std::move(base.collected.placements);
			state->clipped = base.collected.clipped;
			state->spawnOffsets.clear();
			for (const pcg::Placement& placement : state->spawns)
			{
				state->spawnOffsets.push_back(placement.position.y - ground(placement.position.x, placement.position.z));
			}

			state->warnings = std::move(warnings);
			const std::vector<String>& terrainWarnings = state->terrain.GetWarnings();
			state->warnings.insert(state->warnings.end(), terrainWarnings.begin(), terrainWarnings.end());
			return true;
		};

		// Each Apply pass asks only for its channel, so the splat and water passes skip the height ops.
		content.terrain = [state](const float x, const float z, const float baseHeight, const pcg::terrain_channel::Type channels)
		{
			return state->terrain.Evaluate(x, z, baseHeight, state->lookup, &state->notes, channels);
		};

		content.placements = [state](const GroundFunction& ground, const GroundFunction&, std::vector<pcg::Placement>& out)
		{
			// Spawns keep their height above the ground, now the final one.
			out.clear();
			std::set<uint64> keys;
			for (size_t i = 0; i < state->spawns.size(); ++i)
			{
				pcg::Placement placement = state->spawns[i];
				placement.position.y = ground(placement.position.x, placement.position.z) + state->spawnOffsets[i];
				keys.insert(placement.key);
				out.push_back(std::move(placement));
			}

			std::vector<pcg::Placement> sectionPlacements;
			state->terrain.BuildPlacements(ground, sectionPlacements, &state->warnings);
			state->sectionPlacements = 0;
			for (pcg::Placement& placement : sectionPlacements)
			{
				placement.key = pcg::Hash(placement.key, 0, 0, SectionPlacementKeySalt);
				if (!keys.insert(placement.key).second)
				{
					state->warnings.push_back("a section placement (" + placement.asset + ") was dropped: its key collides with another placement");
					continue;
				}
				out.push_back(std::move(placement));
				++state->sectionPlacements;
			}

			std::sort(out.begin(), out.end(), [](const pcg::Placement& a, const pcg::Placement& b)
			{
				return a.key < b.key;
			});
		};

		report = ApplyGeneratedContent(editor, content, record.content, record.id, GraphInstanceLabel);
		report.warnings.insert(report.warnings.end(), state->warnings.begin(), state->warnings.end());
		state->terrain.DescribeNotes(state->notes, report.warnings);
		report.milliseconds = MillisecondsSince(start);
		if (!report.ok)
		{
			report.error = "Rebuild of '" + record.name + "' failed: " + report.error;
			ELOG("PCG: " << report.error);
		}
		else
		{
			// The terrain write succeeded: the river paths of the terrain that was actually applied join the world's flow.
			std::vector<terrain_io::water_flow::FlowPath> flowPaths;
			state->terrain.GetFlowPaths(flowPaths);
			ApplyInstanceFlow(editor, record, controls, std::move(flowPaths), report.warnings);
			report.milliseconds = MillisecondsSince(start);

			ILOG("PCG: graph instance '" << record.name << "' (" << FormatId(record.id) << "): " << state->spawns.size()
				<< " spawn placement(s) inside the area, " << state->clipped << " clipped, " << state->sectionPlacements
				<< " section placement(s), " << report.terrainSamples << " terrain sample(s) written"
				<< (state->reusedDryRun ? " (no previous contribution: the dry run was reused)" : ""));
		}
		return report;
	}

	GeneratorBuildReport DeleteGraphInstanceContent(IWorldEditor& editor, pcg::GraphInstanceRecord& record, const ProceduralEditMode& controls)
	{
		GeneratorBuildReport report = RemoveGeneratedContent(editor, record.content);
		if (report.ok)
		{
			ApplyInstanceFlow(editor, record, controls, {}, report.warnings);
		}
		return report;
	}
}
