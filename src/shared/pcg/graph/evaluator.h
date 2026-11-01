// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "node_registry.h"

#include "math/path_spline.h"
#include "math/vector3.h"

#include <cstddef>
#include <map>
#include <vector>

namespace mmo::pcg::graph
{
	/// @brief A control type merged with a placed control (defined in controls.h).
	struct ResolvedControl;

	/// @brief Read-only terrain access for nodes that read the context.
	struct ITerrainSampler
	{
		virtual ~ITerrainSampler() = default;

		/// @brief Gets the terrain height at a world position. False if there is no terrain there.
		virtual bool Height(float x, float z, float& h) const = 0;

		/// @brief Gets the terrain normal at a world position.
		virtual Vector3 Normal(float x, float z) const = 0;

		/// @brief Gets the blend weight [0,1] of a terrain texture at a world position.
		virtual float SplatWeight(const String& textureKey, float x, float z) const = 0;

		/// @brief Gets the water level at a world position. False if there is no water there.
		virtual bool WaterLevel(float x, float z, float& level) const = 0;

		/// @brief Changes whenever any terrain data changes (part of the evaluator's context hash).
		virtual uint64 Revision() const = 0;
	};

	/// @brief Everything a graph evaluation reads besides the graph itself.
	struct EvalContext
	{
		/// @brief Seed of the graph instance; node seed = pcg::Hash(instanceSeed, node id).
		uint64 instanceSeed = 1;

		/// @brief Region the instance generates into.
		PathSpline region;

		/// @brief Graph parameter overrides. Evaluate hands nodes a copy of the context in which this holds the
		///        overrides merged over the graph's parameter defaults (see MergeParameters).
		AttributeSet parameters;

		/// @brief Set by Evaluate on the context it hands to nodes: the problems found while merging the
		///        parameters (overrides whose type differs from the default). Callers leave it empty.
		std::vector<String> parameterWarnings;

		/// @brief Placed controls visible to the graph (may be null).
		const std::vector<ResolvedControl>* controls = nullptr;

		/// @brief Terrain access (may be null).
		const ITerrainSampler* terrain = nullptr;

		/// @brief Set by the caller whenever *controls changes (normally ControlsHash(*controls)). The
		///        evaluator does not look inside controls; a stale revision means stale cached results.
		uint64 controlsRevision = 0;

		/// @brief Per-node cap on produced points.
		size_t pointLimit = 2'000'000;
	};

	/// @brief Merges graph parameter overrides over the graph's parameter defaults. An override replaces the
	///        default of the same name only if it has the default's type (otherwise the default stays and a
	///        warning is added); overrides of undeclared parameters are kept as they are.
	/// @param warnings Optional; receives one message per rejected override.
	AttributeSet MergeParameters(const std::vector<ParameterDef>& defaults, const AttributeSet& overrides, std::vector<String>* warnings);

	/// @brief Result of one node in an evaluation.
	struct NodeResult
	{
		NodeOutput output;

		/// @brief Hash over every output pin's name and content hash (0 for a failed node).
		uint64 hash = 0;

		/// @brief Whether the output was served from the cache instead of executing the node.
		bool fromCache = false;

		/// @brief Wall time of the execution (0 when served from cache). Not part of any hash.
		double milliseconds = 0;
	};

	struct EvalResult;
	class GraphCache;
	EvalResult Evaluate(const Graph& g, const NodeRegistry& r, const EvalContext& ctx, GraphCache* cache);

	/// @brief Outputs of earlier evaluations, one entry per node id. An entry is reused when the node's
	///        key matches: hash of (node id, node type, effective settings, input content hashes in link
	///        order, node seed and, only for readsContext nodes, the context hash). The context hash covers
	///        the merged parameters and their merge warnings, ctx.region, ctx.controlsRevision,
	///        ctx.terrain->Revision() and ctx.pointLimit.
	///        Failed nodes are not cached; entries of nodes no longer in the graph are dropped.
	class GraphCache
	{
	public:
		/// @brief Removes every entry.
		void Clear();

		/// @brief Gets the number of cached nodes.
		size_t Size() const;

	private:
		struct Entry
		{
			uint64 key = 0;
			NodeOutput output;
			std::map<String, uint64> pinHashes;
			uint64 hash = 0;
		};

		std::map<uint32, Entry> m_entries;

		friend EvalResult Evaluate(const Graph& g, const NodeRegistry& r, const EvalContext& ctx, GraphCache* cache);
	};

	/// @brief Result of a graph evaluation.
	struct EvalResult
	{
		/// @brief One result per node that was evaluated (every node when the graph is valid).
		std::map<uint32, NodeResult> nodes;

		/// @brief Validation issues; when non-empty nothing was executed.
		std::vector<GraphIssue> issues;

		/// @brief True if the graph validated and was evaluated. Individual nodes may still have failed
		///        (see NodeResult::output.error); their downstream nodes report "upstream failed".
		bool ok = false;
	};

	/// @brief Validates and evaluates a graph. Nodes run in topological order, choosing the smallest id among
	///        ready nodes. An input pin receives the concatenation of its linked outputs in link order.
	///        Declared settings missing from the instance take the def's default. Nodes see ctx.parameters
	///        merged over the graph's parameter defaults (MergeParameters). Declared output pins are
	///        always present (empty if not produced); undeclared output pins are dropped with a warning.
	/// @param cache Optional cache that is read and updated.
	EvalResult Evaluate(const Graph& g, const NodeRegistry& r, const EvalContext& ctx, GraphCache* cache);
}
