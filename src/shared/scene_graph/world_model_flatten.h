// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/matrix4.h"
#include "scene_graph/mesh.h"
#include "scene_graph/world_model.h"

#include <vector>

namespace mmo
{
	/// @brief A position, orientation and scale, composed the way SceneNode derives a child's
	///        transform from its parent's (see Node::UpdateFromParentImpl). Unlike multiplying
	///        matrices, this matches what the scene shows under a non-uniform or mirrored parent
	///        scale with rotated children.
	struct FlattenTransform
	{
		Vector3 position { 0.0f, 0.0f, 0.0f };
		Quaternion orientation { Quaternion::Identity };
		Vector3 scale { 1.0f, 1.0f, 1.0f };

		/// @brief Decomposes an affine matrix (a mirroring matrix loses its sign into the rotation).
		static FlattenTransform FromMatrix(const Matrix4& matrix);

		/// @brief The transform as a matrix (MakeTransform).
		[[nodiscard]] Matrix4 ToMatrix() const;

		/// @brief The derived transform of a child placed at the given local transform:
		///        orientation * localOrientation, scale * localScale (componentwise) and
		///        position + orientation * (scale * localPosition).
		[[nodiscard]] FlattenTransform Compose(const Vector3& localPosition, const Quaternion& localOrientation, const Vector3& localScale) const;

		/// @brief A local point in this transform's parent space: position + orientation * (scale * point).
		[[nodiscard]] Vector3 TransformPoint(const Vector3& point) const;
	};

	/// @brief One mesh a world model places, resolved to world space.
	struct FlattenedMeshRef
	{
		/// @brief Path of the mesh file (.hmsh).
		String meshPath;

		/// @brief Mesh-to-world transform, composed through every parent world model.
		Matrix4 world;

		/// @brief Material overrides. Empty: the mesh's own materials. One entry: replaces the
		///        material of every submesh (how world model mesh references and kit props override).
		///        More entries: one per submesh, where an empty entry keeps that submesh's material.
		std::vector<String> materialOverrides;
	};

	/// @brief A particle system (.hpar) a world model places, resolved to world space.
	struct FlattenedParticles
	{
		/// @brief Path of the particle system file.
		String path;

		/// @brief Emitter-to-world transform.
		Matrix4 world;
	};

	/// @brief Controls what FlattenWorldModelExterior and FlattenWorldModelExtras walk.
	struct FlattenOptions
	{
		/// @brief Doodad set placed in addition to the global set 0 (same rule as WorldModelInstance).
		///        Applies to the top-level model only; child world models place their set 0, as their
		///        instances do.
		uint32 doodadSet = 0;

		/// @brief Whether doodads are followed.
		bool includeDoodads = true;

		/// @brief Whether the prop kits in propKitPaths are followed.
		bool includePropKits = true;

		/// @brief Prop kits (.hkit) dressing the top-level model, in application order. Child world
		///        models are never dressed, as their instances are not.
		std::vector<String> propKitPaths;

		/// @brief Deepest nesting of child world models that is followed (WorldModelInstance::MaxChildDepth).
		uint32 maxChildDepth = 8;

		/// @brief Whether pure interior groups, and the doodads, kit props and lights inside them, are left
		///        out. A pure interior group has the Interior flag and not the Exterior flag.
		bool skipInterior = true;

		/// @brief With skipInterior, still keep the mesh references of pure interior groups (not their
		///        particle systems). Their doodads, kit props, lights and child world models stay skipped.
		///        For houses whose outer facades live in interior-flagged storey groups, which the live
		///        client draws from outside through its portal culling.
		bool includeInteriorGroupMeshes = false;
	};

	/// @brief What a flatten walked and left out, for statistics and dependency tracking.
	struct FlattenReport
	{
		/// @brief Pure interior groups that were skipped (counted once per placed occurrence).
		uint32 skippedInteriorGroups = 0;

		/// @brief Pure interior groups whose mesh references were kept (includeInteriorGroupMeshes),
		///        counted once per placed occurrence. Not counted in skippedInteriorGroups.
		uint32 includedInteriorGroups = 0;

		/// @brief Mesh references, doodads and kit props left out because they lie in an interior group.
		uint32 skippedInteriorMeshes = 0;

		/// @brief Child world models skipped because they could not be loaded, nest too deep or nest
		///        a model inside itself.
		uint32 skippedChildren = 0;

		/// @brief Child world models skipped because their origin lies in a pure interior group of
		///        their parent (furniture or rooms modelled as separate world models).
		uint32 skippedInteriorChildren = 0;

		/// @brief Paths of every child world model file read (the top-level model is not included).
		std::vector<String> worldModelPaths;

		/// @brief Paths of every prop kit file read.
		std::vector<String> propKitPaths;
	};

	/// @brief Lights, sounds and particles a world model places, resolved to world space.
	struct FlattenedWorldModelExtras
	{
		/// @brief Lights of the models and their prop kits. Position and rotation are in world space;
		///        ranges and cone angles are left unscaled, as scene lights under a scaled node are.
		///        Ambient lights are left out, as WorldModelInstance creates no scene light for them.
		std::vector<WorldModelLight> lights;

		/// @brief Every mesh path the walk loaded (or tried to load) for its sound emitters.
		std::vector<String> meshPaths;

		/// @brief Mesh sound emitters of every mesh placed (interior meshes included: a sound inside a
		///        building is audible outside), like WorldModelInstance::CollectSoundEmitters.
		std::vector<PlacedSoundEmitter> sounds;

		/// @brief Particle systems referenced by mesh references, doodads or kit props (paths ending
		///        in .hpar), outside interior groups.
		std::vector<FlattenedParticles> particles;
	};

	/// @brief Whether a group flag set is a pure interior group (Interior flag set, Exterior flag not).
	bool IsPureInteriorWorldModelGroup(uint32 groupFlags);

	/// @brief Finds the group a model-space point lies in, like WorldModelInstance does for doodads:
	///        only when the model defines containment volumes, the smallest group containing the point.
	/// @return The group index, or -1 when the point lies in no group or the model has no volumes.
	int32 FindWorldModelGroupContaining(const WorldModel& model, const Vector3& localPoint);

	/// @brief Lists every mesh a placed world model shows from outside: the visible mesh references of
	///        its non-interior groups, the doodads of the active sets and the kit props (both skipped
	///        inside interior groups), and recursively the same of its child world models with their
	///        transforms (composed like SceneNode, see FlattenTransform). Child world models whose origin
	///        lies in a pure interior group are skipped like doodads there. Particle system references
	///        (.hpar) are left out; see FlattenWorldModelExtras.
	/// @details Works on the model data only; needs no scene and no graphics device. Child models and
	///          prop kits are loaded through WorldModelManager and PropKitManager.
	/// @param model The world model to flatten. Null gives an empty list.
	/// @param transform Model-to-world transform of the placement.
	/// @param options What to follow.
	/// @param report Optional: receives what was skipped and which files were read.
	std::vector<FlattenedMeshRef> FlattenWorldModelExterior(const WorldModelPtr& model, const Matrix4& transform, const FlattenOptions& options, FlattenReport* report = nullptr);

	/// @brief FlattenWorldModelExterior for a placement given as position, orientation and scale,
	///        which is how the scene holds it; avoids decomposing a matrix.
	std::vector<FlattenedMeshRef> FlattenWorldModelExterior(const WorldModelPtr& model, const FlattenTransform& transform, const FlattenOptions& options, FlattenReport* report = nullptr);

	/// @brief Lists the lights, mesh sound emitters and particle systems a placed world model brings
	///        along, walked exactly like FlattenWorldModelExterior. Loads meshes through MeshManager to
	///        read their sound emitters.
	FlattenedWorldModelExtras FlattenWorldModelExtras(const WorldModelPtr& model, const Matrix4& transform, const FlattenOptions& options);

	/// @copydoc FlattenWorldModelExtras
	FlattenedWorldModelExtras FlattenWorldModelExtras(const WorldModelPtr& model, const FlattenTransform& transform, const FlattenOptions& options);

	/// @brief Whether a path names a particle system file (.hpar, case-insensitive).
	bool IsParticleSystemPath(const String& path);
}
