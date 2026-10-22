// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "glue_scene_definition.h"
#include "math/aabb.h"
#include "math/matrix4.h"
#include "math/plane.h"
#include "math/quaternion.h"
#include "math/vector3.h"

#include <functional>
#include <optional>
#include <vector>

namespace mmo
{
	/// A camera pose the glue scene can be seen from.
	struct GlueCameraPose
	{
		Vector3 position;
		Vector3 lookAt;
	};

	/// Samples every camera pose the glue scene can be seen from: pathSamples poses along the
	/// flight path (t from 0 to 1), the gate pose plus two drifted by +-0.3 m sideways, the plaza
	/// pose (last key), a push-in end pose towards each plaza spot and the front spot, and 8
	/// poses along the way from the entrance to the front spot at eye height.
	std::vector<GlueCameraPose> SampleGlueCameraPoses(const GlueSceneDefinition& def, uint32 pathSamples = 48);

	/// A set of view frusta, one per camera pose and supported aspect ratio.
	struct GlueBakeFrustumSet
	{
		/// One view frustum: six inward facing planes (near, far, left, right, top, bottom).
		struct Frustum
		{
			Vector3 origin;
			Plane planes[6];
		};

		std::vector<Frustum> frusta;

		/// Whether the bounds, grown by margin, intersect any frustum of the set.
		bool Intersects(const AABB& worldBounds, float margin) const;

		/// The largest size in pixels the bounds (its longest extent) can take on a screen of
		/// the given height, taken over every frustum the bounds, grown by margin, are visible in.
		/// The margin only widens which frusta count; the size is that of the bounds themselves.
		/// 0 if invisible.
		float MaxProjectedSizePixels(const AABB& worldBounds, float screenHeightPixels, float margin = 0.0f) const;

		/// Vertical field of view the frusta were built with, in degrees.
		float verticalFovDegrees = 60.0f;
	};

	/// Builds one frustum per pose for each aspect ratio in { 4:3, 16:9, 21:9 }.
	GlueBakeFrustumSet BuildGlueBakeFrusta(const std::vector<GlueCameraPose>& poses, float verticalFovDegrees, float farDistance);

	/// Whether a WMO group is a pure interior group (indoor flag set, outdoor flag not), which
	/// can never be seen from the glue cameras.
	bool IsGlueBakeInteriorGroup(uint32 groupFlags);

	/// A source file the bake depends on, with its content hash at bake time.
	struct GlueBakeInputFile
	{
		String path;
		String hash;
	};

	/// A baked static mesh.
	struct GlueBakeMesh
	{
		String mesh;
		Matrix4 transform;

		/// Material per submesh (by index); an empty entry keeps the mesh's own material. Empty when
		/// nothing is overridden (merged chunk meshes carry their materials themselves).
		std::vector<String> materialOverrides;
	};

	/// A baked terrain tile.
	/// Sampler state contract (.htex files carry none): the runtime must set the splatting texture to
	/// Clamp addressing with Anisotropic filtering (as terrain::Tile does with its coverage texture) and
	/// the far tiles' albedo texture (the "Albedo" parameter of their material instance) to Clamp (as
	/// FarTerrain does). With the default Wrap, edge texels bleed across tile and page borders.
	struct GlueBakeTerrainTile
	{
		String mesh;

		/// Near tiles: the tile's layered terrain material, to instance with the splatting texture.
		/// Far tiles: their ready material instance (.hmi), used as is.
		String material;

		/// Splatting (layer coverage) texture for the material's "Splatting" parameter; empty for far tiles.
		String splatting;
	};

	/// A baked foliage instance.
	struct GlueBakeFoliage
	{
		String mesh;
		Vector3 position;
		Quaternion rotation;
		Vector3 scale;
	};

	/// A light carried over from WMOs and prop kits (mirrors WorldModelLight).
	struct GlueBakeLight
	{
		/// 0 omni, 1 spot, 2 direct, 3 ambient.
		uint32 type = 0;
		bool useAttenuation = false;
		uint32 color = 0;
		Vector3 position;
		float intensity = 0.0f;
		Quaternion rotation;
		float attenuationStart = 0.0f;
		float attenuationEnd = 0.0f;
		float innerConeAngle = 0.0f;
		float outerConeAngle = 0.0f;
		float fogScattering = 0.0f;
	};

	/// An ambient looping sound (mirrors AmbientSoundSource).
	struct GlueBakeSound
	{
		uint32 soundEntry = 0;
		Vector3 position;
		float volume = 1.0f;
		float radius = 0.0f;
	};

	/// A particle emitter placement.
	struct GlueBakeParticles
	{
		String file;
		Matrix4 transform;
	};

	/// Describes what a glue bake contains and what it was baked from.
	struct GlueBakeManifest
	{
		uint32 version = 1;

		/// Hex FNV-1a 64 of the GlueScene.json bytes.
		String sceneHash;
		std::vector<GlueBakeInputFile> inputs;
		std::vector<GlueBakeMesh> meshes;
		std::vector<GlueBakeTerrainTile> terrainTiles;

		/// Water surfaces in world space, drawn with their water profile's surface material (stored in
		/// the mesh). They must not cast shadows.
		std::vector<GlueBakeMesh> water;
		std::vector<GlueBakeFoliage> foliage;
		std::vector<GlueBakeLight> lights;
		std::vector<GlueBakeSound> sounds;
		std::vector<GlueBakeParticles> particles;
		AABB bounds;
	};

	/// Serializes a manifest to JSON.
	String WriteGlueBakeManifest(const GlueBakeManifest& manifest);

	/// Parses a manifest. Returns nullopt on malformed JSON, any missing required field or
	/// wrong type, or a version other than 1.
	std::optional<GlueBakeManifest> ParseGlueBakeManifest(const String& json);

	/// FNV-1a 64 of the bytes as 16 lowercase hex characters.
	String HashGlueBakeBytes(const void* data, size_t size);

	/// Whether the client draws the glue scene from the bake: only for a parsed manifest whose
	/// sceneHash matches the hash of the current GlueScene.json (a camera edit without a rebake
	/// falls back to streaming the world).
	/// @param manifest The parsed manifest, or nullopt when it is missing or unparsable.
	/// @param currentSceneHash HashGlueBakeBytes of the current GlueScene.json.
	bool ShouldUseGlueBake(const std::optional<GlueBakeManifest>& manifest, const String& currentSceneHash);

	/// Baked mesh placements that can be drawn with one instanced draw call per submesh.
	struct GlueBakeMeshGroup
	{
		String mesh;
		std::vector<String> materialOverrides;

		/// Whether the placements mirror the mesh (one draw call has one cull mode).
		bool mirrored = false;

		/// The cell of the placements' positions on the XZ plane.
		int32 cellX = 0;
		int32 cellZ = 0;

		/// Indices into the grouped meshes, in input order.
		std::vector<size_t> placements;
	};

	/// Groups baked mesh placements by mesh, material overrides, mirroring and square XZ cell, so
	/// each group can be drawn instanced and still be frustum culled cell by cell.
	/// @param meshes The placements to group.
	/// @param cellSize Edge length of a cell in world units; 0 or less puts everything in one cell.
	/// @return The groups, in a deterministic order (by key).
	std::vector<GlueBakeMeshGroup> GroupGlueBakeMeshes(const std::vector<GlueBakeMesh>& meshes, float cellSize);

	/// Returns the paths of the manifest's inputs whose current hash differs or which are missing.
	/// hashFile returns the current hash of a path, or nullopt if it cannot be read.
	std::vector<String> FindStaleGlueBakeInputs(const GlueBakeManifest& manifest, const std::function<std::optional<String>(const String& path)>& hashFile);
}
