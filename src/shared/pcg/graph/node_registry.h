// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "graph.h"

#include <functional>
#include <map>
#include <vector>

namespace mmo::pcg::graph
{
	struct EvalContext;

	/// @brief Describes an input or output pin of a node type.
	struct PinDef
	{
		/// @brief Pin name, unique per direction within a node type.
		String name;

		/// @brief Item kinds the pin accepts (inputs) or produces (outputs). Empty means any kind.
		std::vector<ItemKind> accepts;

		/// @brief Whether an input pin may have more than one incoming link (ignored for outputs).
		bool multiple = true;

		/// @brief Whether an input pin needs at least one incoming link (ignored for outputs).
		bool required = false;
	};

	/// @brief Describes a node setting. The default value's type is the setting's type.
	struct SettingDef
	{
		String name;
		AttributeValue defaultValue;

		/// @brief UI range for numeric settings (used when max > min).
		float min = 0;
		float max = 0;

		/// @brief Allowed values for an enum-like String setting (empty = free text).
		std::vector<String> enumValues;

		String description;
	};

	/// @brief What a node produced: items per output pin, warnings, and an error (non-empty = failed).
	struct NodeOutput
	{
		std::map<String, Collection> pins;
		std::vector<String> warnings;
		String error;
	};

	/// @brief Executes a node. Must be deterministic: the same arguments produce the same output.
	///        `settings` holds every declared setting (instance value or the def's default), plus any
	///        undeclared instance settings. `inputs` holds one entry per input pin that has links.
	///        Only nodes whose type sets readsContext may read the context.
	using NodeExecute = std::function<NodeOutput(const EvalContext&, uint64 nodeSeed, const AttributeSet& settings, const std::map<String, Collection>& inputs)>;

	/// @brief A node type: pins, settings and the executor.
	struct NodeTypeDef
	{
		/// @brief Unique type name stored in graph files.
		String type;

		/// @brief Palette category and title (UI only).
		String category;
		String title;

		std::vector<PinDef> inputs;
		std::vector<PinDef> outputs;
		std::vector<SettingDef> settings;

		/// @brief Whether the node reads anything from the EvalContext besides its seed (parameters,
		///        controls, terrain, region, point limit). Such nodes are re-run when the context changes.
		bool readsContext = false;

		NodeExecute execute;
	};

	/// @brief The set of node types a graph can use.
	class NodeRegistry
	{
	public:
		/// @brief Registers a node type, replacing a previous registration of the same type name.
		void Register(NodeTypeDef def);

		/// @brief Finds a node type by name, or nullptr. The pointer is valid until the type is re-registered.
		const NodeTypeDef* Find(const String& type) const;

		/// @brief Gets every node type, sorted by category, then title, then type name.
		std::vector<const NodeTypeDef*> All() const;

		/// @brief Gets the registry with every built-in node type registered (see RegisterBuiltinNodes).
		static NodeRegistry& Default();

	private:
		std::map<String, NodeTypeDef> m_types;
	};

	/// @brief Registers every built-in node type. Each node family lives in its own translation unit
	///        and exposes a Register*Nodes function that is called from here explicitly, so a static
	///        library link cannot drop it.
	void RegisterBuiltinNodes(NodeRegistry& registry);

	/// @brief A validation problem. node is the offending node's id (0 for graph-level problems).
	struct GraphIssue
	{
		uint32 node = 0;
		String message;
	};

	/// @brief Validates a graph against a registry: invalid or duplicate node ids, unknown node types,
	///        dangling links, unknown pins, pin kind mismatches, several links into a single-link pin,
	///        cycles, missing required inputs and setting type mismatches (declared settings must hold the
	///        default value's type exactly; undeclared settings are ignored).
	/// @return The issues in a deterministic order; empty if the graph is valid.
	std::vector<GraphIssue> ValidateGraph(const Graph& g, const NodeRegistry& r);
}
