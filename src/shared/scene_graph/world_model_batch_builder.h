// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/matrix4.h"
#include "math/vector4.h"

#include <functional>
#include <vector>

namespace mmo
{
	/// @brief Sentinel group index meaning "belongs to no room", i.e. never portal-culled.
	constexpr size_t WorldModelNoGroup = static_cast<size_t>(-1);

	/// @brief One thing to place inside a world model: which room, which mesh, and where.
	/// @details Deliberately decoupled from WorldModel so that both mesh references and doodads can
	///          feed the same bucketing pass, and so the pass can be unit tested without assets.
	struct WorldModelPlacementInput
	{
		/// @brief Index of the room this placement belongs to, or WorldModelNoGroup.
		size_t groupIndex { WorldModelNoGroup };

		/// @brief Index into the caller's own list, so a placement can be traced back to the mesh
		///        reference or doodad it came from.
		size_t sourceIndex { 0 };

		/// @brief Path of the mesh asset to place.
		String meshPath;

		/// @brief Material override path, or empty to use the submesh's own material.
		String materialOverride;

		/// @brief Transform relative to the world model, before the placement transform is applied.
		Matrix4 localTransform { Matrix4::Identity };

		/// @brief Per-instance tint. White leaves the mesh unchanged.
		Vector4 tint { 1.0f, 1.0f, 1.0f, 1.0f };
	};

	/// @brief Identifies one instanced draw call.
	/// @details Room is part of the key because portal culling toggles visibility per room; batching
	///          across rooms would defeat it. Submesh is part of it because one draw call covers one
	///          index range and one material. Material override is part of it because an override
	///          replaces the material of every submesh, so an overridden placement cannot share a
	///          draw with a non-overridden one.
	struct WorldModelBatchKey
	{
		size_t groupIndex { WorldModelNoGroup };
		String meshPath;
		uint16 submeshIndex { 0 };
		String materialOverride;

		bool operator<(const WorldModelBatchKey& other) const;
	};

	/// @brief A set of placements that can be drawn with one DrawIndexedInstanced.
	struct WorldModelBucket
	{
		WorldModelBatchKey key;
		std::vector<WorldModelPlacementInput> placements;
	};

	/// @brief What the bucketing pass needs to know about a mesh asset, without loading it.
	struct WorldModelMeshFacts
	{
		/// @brief Number of submeshes; each becomes its own bucket.
		uint16 submeshCount { 0 };

		/// @brief Whether this mesh may be drawn through the instanced path at all. False for
		///        skinned meshes (the device picks the instanced vertex shader unconditionally when
		///        an instance buffer is bound, so bone data would be ignored) and for meshes whose
		///        submeshes carry no index data (the instanced path only draws indexed geometry).
		bool batchable { false };
	};

	/// @brief Resolves a mesh path to its facts, or nullptr when the mesh cannot be loaded.
	using WorldModelMeshLookup = std::function<const WorldModelMeshFacts*(const String&)>;

	/// @brief Groups placements into instanced-draw buckets.
	/// @details Placements whose mesh cannot be resolved or is not batchable, and buckets that end up
	///          with fewer than minInstancesPerBatch placements, are returned through outSingletons
	///          for the caller to render the ordinary per-entity way. A single-placement bucket is
	///          one draw call either way, so batching it would add an instance buffer and a
	///          dependency on the material's instanced shader variant for no gain.
	/// @param placements The placements to group. Order within a bucket follows this order.
	/// @param meshLookup Resolves mesh facts. Called at most once per distinct mesh path.
	/// @param minInstancesPerBatch Smallest bucket worth batching.
	/// @param outBuckets Receives the buckets worth batching, in a deterministic key order.
	/// @param outSingletons Receives every placement that should stay on the per-entity path.
	void BuildWorldModelBuckets(
		const std::vector<WorldModelPlacementInput>& placements,
		const WorldModelMeshLookup& meshLookup,
		size_t minInstancesPerBatch,
		std::vector<WorldModelBucket>& outBuckets,
		std::vector<WorldModelPlacementInput>& outSingletons);
}
