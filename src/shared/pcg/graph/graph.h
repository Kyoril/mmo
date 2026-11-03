// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "graph_data.h"

#include <vector>

namespace mmo::pcg::graph
{
	/// @brief Highest valid node id. Valid ids are 1..MaxNodeId; 0xFFFFFFFF is reserved so that "highest id + 1"
	///        (Graph::nextNodeId) never wraps to 0.
	constexpr uint32 MaxNodeId = 0xFFFFFFFEu;

	/// @brief One node placed in a graph.
	struct NodeInstance
	{
		/// @brief Unique id within the graph, 1..MaxNodeId.
		uint32 id = 0;

		/// @brief Node type name, resolved by the registry.
		String type;

		/// @brief Per-node settings.
		AttributeSet settings;

		/// @brief Editor canvas position.
		float editorX = 0;
		float editorY = 0;

		/// @brief Whether the editor shows this node's output.
		bool debug = false;

		/// @brief Free-form user comment.
		String comment;
	};

	/// @brief A connection from an output pin of one node to an input pin of another.
	struct Link
	{
		uint32 fromNode = 0;
		String fromPin;
		uint32 toNode = 0;
		String toPin;
	};

	/// @brief A graph parameter that can be overridden by the user of the graph.
	struct ParameterDef
	{
		String name;
		AttributeValue defaultValue;
		String description;
	};

	/// @brief The graph model (no validation; see the registry-aware validator).
	struct Graph
	{
		String name;
		std::vector<NodeInstance> nodes;
		std::vector<Link> links;
		std::vector<ParameterDef> parameters;

		/// @brief Next id handed out to a new node. MaxNodeId + 1 means no id is left.
		uint32 nextNodeId = 1;
	};
}
