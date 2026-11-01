// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

namespace mmo::pcg::graph
{
	class NodeRegistry;

	/// @brief Registers the input nodes: GetControls, GraphParameters and GetRegion (nodes_input.cpp).
	///        There is no Get Terrain data node: nodes that need terrain read it through EvalContext::terrain
	///        (and set readsContext).
	void RegisterInputNodes(NodeRegistry& registry);

	/// @brief Registers the sampler nodes: SampleSpline, SampleArea and SampleTerrainSurface (nodes_sampler.cpp).
	void RegisterSamplerNodes(NodeRegistry& registry);
}
