// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "graph.h"

namespace mmo::pcg::graph
{
	/// @brief Parses a .hpcgg graph file (JSON, "version": 1). Unknown keys are ignored.
	/// @param json File content.
	/// @param out Receives the graph on success; left untouched on failure.
	/// @param error Receives a message on failure.
	/// @return False on malformed JSON, unsupported version, missing/invalid fields or duplicate node ids.
	bool ParseGraph(const String& json, Graph& out, String& error);

	/// @brief Writes a graph as .hpcgg text. Deterministic: nodes sorted by id, links sorted, keys sorted,
	///        2-space indent. Parameters keep their order.
	String WriteGraph(const Graph& graph);
}
