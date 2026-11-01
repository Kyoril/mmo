// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

namespace mmo::pcg::graph
{
	class NodeRegistry;

	/// @brief Registers the input nodes: GetControls, GraphParameters and GetRegion (nodes_input.cpp).
	///        There is no Get Terrain data node: nodes that need terrain read it through EvalContext::terrain
	///        (and declare context_dep::Terrain).
	void RegisterInputNodes(NodeRegistry& registry);

	/// @brief Registers the sampler nodes: SampleSpline, SampleArea and SampleTerrainSurface (nodes_sampler.cpp).
	void RegisterSamplerNodes(NodeRegistry& registry);

	/// @brief Registers the filter and spatial nodes: FilterByTag, FilterByAttribute, FilterByTerrain, DensityFilter,
	///        SelfPrune, Difference, Intersection and DistanceTo (nodes_filter.cpp).
	void RegisterFilterNodes(NodeRegistry& registry);

	/// @brief Registers the point operation nodes: TransformPoints, DensityNoise, AttributeMath and
	///        CopyAttributesFromControl (nodes_point.cpp).
	void RegisterPointNodes(NodeRegistry& registry);

	/// @brief Registers the spawner nodes: SpawnMeshes (nodes_spawn.cpp).
	void RegisterSpawnNodes(NodeRegistry& registry);
}
