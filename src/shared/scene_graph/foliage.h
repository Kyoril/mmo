// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "foliage_layer.h"
#include "foliage_chunk.h"
#include "base/typedefs.h"
#include "math/aabb.h"
#include "math/vector3.h"

#include <functional>
#include <map>
#include <memory>
#include <random>
#include <vector>

namespace mmo
{
	class Scene;
	class SceneNode;
	class Camera;
	class GraphicsDevice;

	/// @brief Callback function type for getting terrain height at a position.
	/// @param x World X coordinate.
	/// @param z World Z coordinate.
	/// @param outHeight Output height value.
	/// @param outNormal Output surface normal.
	/// @return True if a valid height was found, false otherwise.
	using HeightQueryCallback = std::function<bool(float x, float z, float& outHeight, Vector3& outNormal)>;

	/// @brief Settings for the foliage system.
	struct FoliageSettings
	{
		/// @brief Size of each foliage chunk in world units.
		float chunkSize = 32.0f;

		/// @brief Maximum distance at which foliage is visible.
		float maxViewDistance = 150.0f;

		/// @brief Number of chunks to keep loaded around the camera.
		int32 loadRadius = 5;

		/// @brief Whether to enable frustum culling for chunks.
		bool frustumCulling = true;

		/// @brief Whether to enable distance-based LOD.
		bool enableLOD = true;

		/// @brief Global density multiplier (0.0 to 1.0).
		float globalDensityMultiplier = 1.0f;
	};

	/// @brief Key for identifying a chunk by its coordinates.
	struct ChunkKey
	{
		int32 x;
		int32 z;

		bool operator<(const ChunkKey& other) const
		{
			if (x != other.x)
			{
				return x < other.x;
			}
			return z < other.z;
		}

		bool operator==(const ChunkKey& other) const
		{
			return x == other.x && z == other.z;
		}
	};

	/// @brief Main foliage system class that manages foliage layers and rendering.
	/// @details The Foliage class provides efficient grass and vegetation rendering
	///          using GPU instancing. It divides the world into chunks and generates
	///          foliage instances based on layer settings and terrain data.
	class Foliage final
	{
	public:
		/// @brief Creates a new foliage system.
		/// @param scene The scene to add foliage to.
		/// @param device The graphics device for creating GPU resources.
		explicit Foliage(Scene& scene, GraphicsDevice& device);

		~Foliage();

	public:
		/// @brief Gets the current foliage settings.
		[[nodiscard]] const FoliageSettings& GetSettings() const
		{
			return m_settings;
		}

		/// @brief Sets the foliage settings.
		/// @param settings The new settings to apply.
		void SetSettings(const FoliageSettings& settings);

		/// @brief Sets the height query callback for terrain sampling.
		/// @param callback Function to query terrain height and normal.
		void SetHeightQueryCallback(HeightQueryCallback callback);

		/// @brief Adds a new foliage layer.
		/// @param layer The layer to add.
		void AddLayer(const FoliageLayerPtr& layer);

		/// @brief Removes a foliage layer by name.
		/// @param name The name of the layer to remove.
		/// @return True if the layer was found and removed.
		bool RemoveLayer(const String& name);

		/// @brief Gets a foliage layer by name.
		/// @param name The name of the layer to find.
		/// @return The layer, or nullptr if not found.
		[[nodiscard]] FoliageLayerPtr GetLayer(const String& name) const;

		/// @brief Gets all foliage layers.
		[[nodiscard]] const std::vector<FoliageLayerPtr>& GetLayers() const
		{
			return m_layers;
		}

		/// @brief Clears all layers and chunks.
		void Clear();

		/// @brief Updates the foliage system based on camera position.
		/// @param camera The current camera for visibility calculations.
		void Update(Camera& camera);

		/// @brief Rebuilds all foliage chunks.
		/// @details Call this after modifying layer settings.
		void RebuildAll();

		/// @brief Rebuilds foliage in a specific world region.
		/// @param region The AABB region to rebuild.
		void RebuildRegion(const AABB& region);

		/// @brief Gets the total number of active chunks.
		[[nodiscard]] size_t GetActiveChunkCount() const
		{
			return m_activeChunks.size();
		}

		/// @brief Gets the total number of foliage instances across all chunks.
		[[nodiscard]] size_t GetTotalInstanceCount() const;

		/// @brief Sets whether the foliage system is visible.
		/// @param visible True to show foliage, false to hide.
		void SetVisible(bool visible);

		/// @brief Gets whether the foliage system is visible.
		[[nodiscard]] bool IsVisible() const
		{
			return m_visible;
		}

		/// @brief Gets the scene this foliage system belongs to.
		[[nodiscard]] Scene& GetScene() const
		{
			return m_scene;
		}

		/// @brief Gets the graphics device used by this foliage system.
		[[nodiscard]] GraphicsDevice& GetGraphicsDevice() const
		{
			return m_device;
		}

		/// @brief Gets the root scene node for foliage.
		[[nodiscard]] SceneNode* GetRootNode() const
		{
			return m_rootNode;
		}

		/// @brief Sets the bounds within which foliage can be generated.
		/// @param bounds The world-space bounds.
		void SetBounds(const AABB& bounds);

		/// @brief Gets the current foliage bounds.
		[[nodiscard]] const AABB& GetBounds() const
		{
			return m_bounds;
		}

	private:
		/// @brief Generates foliage instances for a chunk.
		/// @param chunk The chunk to populate.
		/// @param layer The layer to generate instances for.
		void GenerateChunkInstances(FoliageChunk& chunk, const FoliageLayer& layer);

		/// @brief Creates or retrieves a chunk at the given coordinates.
		/// @param chunkX Chunk X coordinate.
		/// @param chunkZ Chunk Z coordinate.
		/// @param layer The layer this chunk belongs to.
		/// @return The chunk at the specified location.
		FoliageChunkPtr GetOrCreateChunk(int32 chunkX, int32 chunkZ, const FoliageLayerPtr& layer);

		/// @brief Converts world position to chunk coordinates.
		/// @param worldPos World position.
		/// @param chunkX Output chunk X coordinate.
		/// @param chunkZ Output chunk Z coordinate.
		void WorldToChunk(const Vector3& worldPos, int32& chunkX, int32& chunkZ) const;

		/// @brief Updates which chunks should be active based on camera position.
		/// @param camera The current camera.
		void UpdateActiveChunks(Camera& camera);

		/// @brief Unloads chunks that are too far from the camera.
		/// @param cameraChunkX Camera's chunk X coordinate.
		/// @param cameraChunkZ Camera's chunk Z coordinate.
		void UnloadDistantChunks(int32 cameraChunkX, int32 cameraChunkZ);

	private:
		Scene& m_scene;
		GraphicsDevice& m_device;
		SceneNode* m_rootNode = nullptr;

		FoliageSettings m_settings;
		HeightQueryCallback m_heightQuery;

		std::vector<FoliageLayerPtr> m_layers;

		/// @brief Map from layer name + chunk key to chunk.
		std::map<std::pair<String, ChunkKey>, FoliageChunkPtr> m_chunks;

		/// @brief Currently active (visible) chunks.
		std::vector<FoliageChunkPtr> m_activeChunks;

		/// @brief World bounds for foliage generation.
		AABB m_bounds;

		/// @brief Whether the foliage system is visible.
		bool m_visible = true;

		/// @brief Last camera position used for chunk loading.
		Vector3 m_lastCameraPosition;

		/// @brief Whether a full rebuild is needed.
		bool m_needsRebuild = false;
	};
}
