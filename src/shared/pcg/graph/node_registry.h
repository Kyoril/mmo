// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "graph.h"

#include <functional>
#include <map>
#include <vector>

namespace mmo::pcg::graph
{
	struct EvalContext;

	/// @brief Parts of the EvalContext a node type reads (bit mask). A node's cache key covers exactly the
	///        parts it declares, so a node is re-run only when one of them changes.
	namespace context_dep
	{
		enum Type : uint32
		{
			/// @brief Reads nothing from the context besides the node seed.
			None = 0,

			/// @brief ctx.parameters and ctx.parameterWarnings. Settings bound to a parameter ("$name") do
			///        not need this: the resolved value is part of the effective settings.
			Parameters = 1,

			/// @brief ctx.region.
			Region = 2,

			/// @brief ctx.controls (presence and ctx.controlsRevision).
			Controls = 4,

			/// @brief ctx.terrain (presence and Revision()).
			Terrain = 8,

			/// @brief ctx.pointLimit.
			PointLimit = 16
		};
	}

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
	///
	///        Parameter bindings: a declared setting whose instance value is the String "$name" takes the value
	///        of the graph parameter `name` (the caller's override merged over the graph's default) instead,
	///        for settings of every type, String settings included. "$$..." is the literal String "$..." (one
	///        "$" removed). The parameter must be declared by the graph (Graph::parameters) with the setting's
	///        type, else ValidateGraph reports an issue (and the node fails if it is executed anyway). The
	///        evaluator resolves bindings before a node runs, so the node sees, and its cache key covers, the
	///        resolved value. Undeclared instance settings are never resolved or unescaped.
	struct SettingDef
	{
		String name;
		AttributeValue defaultValue;

		/// @brief UI range for numeric settings (used when max > min).
		float min = 0;
		float max = 0;

		/// @brief Allowed values for an enum-like String setting (empty = free text). Any other value is a
		///        validation issue, and a node error when it reaches the node through a parameter override.
		std::vector<String> enumValues;

		String description;

		/// @brief Editor hint only (not part of any cache key or validation): "" (plain), "asset:<ext;ext>"
		///        (one asset path), "assetList:<ext;ext>" (weighted asset list) or "color".
		String widget = {};
	};

	/// @brief What a node produced: items per output pin, warnings, and an error (non-empty = failed).
	struct NodeOutput
	{
		std::map<String, Collection> pins;
		std::vector<String> warnings;
		String error;
	};

	/// @brief Executes a node. Must be deterministic: the same arguments produce the same output.
	///        `settings` holds every declared setting (instance value with parameter bindings resolved, or
	///        the def's default), plus any undeclared instance settings. `inputs` holds one entry per input
	///        pin that has links.
	///        A node may read only the context parts its type declares in contextDeps.
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

		/// @brief The context parts the node reads (context_dep::Type bits). Every part a node's executor
		///        reads must be declared, or cached results go stale; executors of None nodes take an unnamed
		///        context parameter.
		uint32 contextDeps = context_dep::None;

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

	/// @brief Whether a setting value binds to a graph parameter: a String starting with "$" but not "$$".
	/// @param parameterName Receives the parameter name (the text after "$"; may be empty) for a binding.
	bool IsParameterBinding(const AttributeValue& value, String& parameterName);

	/// @brief The literal value of a setting value that is not a binding: "$$..." becomes "$...", any other
	///        value is returned unchanged.
	AttributeValue UnescapeSettingValue(const AttributeValue& value);

	/// @brief A validation problem. node is the offending node's id (0 for graph-level problems).
	struct GraphIssue
	{
		uint32 node = 0;
		String message;
	};

	/// @brief Validates a graph against a registry: invalid (0 or above MaxNodeId) or duplicate node ids,
	///        unknown node types, dangling links, unknown pins, pin kind mismatches, several links into a
	///        single-link pin, cycles, missing required inputs, setting type mismatches (declared settings
	///        must hold the default value's type exactly; undeclared settings are ignored), String values
	///        outside a setting's enumValues (for a binding: the parameter's default) and parameter bindings
	///        to a parameter the graph does not declare or that has another type than the setting.
	/// @return The issues in a deterministic order; empty if the graph is valid.
	std::vector<GraphIssue> ValidateGraph(const Graph& g, const NodeRegistry& r);
}
