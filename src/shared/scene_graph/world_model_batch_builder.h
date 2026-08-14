// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/matrix4.h"
#include "math/quaternion.h"
#include "math/vector3.h"
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

		/// @brief Position relative to the world model.
		Vector3 position { 0.0f, 0.0f, 0.0f };

		/// @brief Rotation relative to the world model.
		Quaternion rotation { Quaternion::Identity };

		/// @brief Scale relative to the world model.
		Vector3 scale { 1.0f, 1.0f, 1.0f };
	};

	/// @brief Composes a placement's world transform under a world model's placement transform.
	/// @details Deliberately mirrors Node::UpdateFromParentImpl component by component rather than
	///          multiplying the two matrices. The scene graph concatenates a parent and child by
	///          multiplying their scales element-wise and their orientations as quaternions, which
	///          only agrees with a matrix product when the parent scale is uniform or the child is
	///          unrotated. Getting this wrong would shear every rotated module of a non-uniformly
	///          scaled world model, and - worse - shear only the batched ones, so batched modules
	///          would no longer line up with the neighbours that batching demoted to entities.
	/// @param parentPosition Derived position of the world model's placement node.
	/// @param parentOrientation Derived orientation of the world model's placement node.
	/// @param parentScale Derived scale of the world model's placement node.
	/// @param placement The placement to transform.
	/// @return The placement's world transform.
	Matrix4 ComposeWorldTransform(
		const Vector3& parentPosition,
		const Quaternion& parentOrientation,
		const Vector3& parentScale,
		const WorldModelPlacementInput& placement);

	/// @brief Identifies one instanced draw call.
	/// @details Room is part of the key because portal culling toggles visibility per room; batching
	///          across rooms would defeat it. Submesh is part of it because one draw call covers one
	///          index range and one material. Material override is part of it because an override
	///          replaces the material of every submesh, so an overridden placement cannot share a
	///          draw with a non-overridden one.
	struct WorldModelBatchKey
	{
		/// @brief Room the placements belong to, or WorldModelNoGroup.
		size_t groupIndex { WorldModelNoGroup };

		/// @brief Path of the shared mesh asset.
		String meshPath;

		/// @brief Submesh of that mesh this draw call covers.
		uint16 submeshIndex { 0 };

		/// @brief Material override path, or empty for the submesh's own material.
		String materialOverride;

		/// @brief Orders keys so bucket output is deterministic across runs.
		bool operator<(const WorldModelBatchKey& other) const;
	};

	/// @brief A set of placements that can be drawn with one DrawIndexedInstanced.
	struct WorldModelBucket
	{
		/// @brief What this bucket draws.
		WorldModelBatchKey key;

		/// @brief The placements to draw, in input order.
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
