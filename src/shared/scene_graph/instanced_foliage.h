// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "foliage_chunk.h"
#include "foliage_layer.h"
#include "instanced_foliage_collision.h"
#include "mesh.h"
#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "math/vector3.h"
#include "math/quaternion.h"
#include "math/matrix4.h"

#include <map>
#include <set>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mmo
{
	class Scene;
	class SceneNode;
	class GraphicsDevice;
	struct Ray;

	/// @brief A single authored, instanced-rendered foliage placement managed by InstancedFoliage.
	/// @details This is the scene-graph side counterpart of the persisted foliage instance. It is
	///          deliberately decoupled from the world-file format so that the renderer does not
	///          depend on the game-data layer.
	struct InstancedFoliageInstance
	{
		/// @brief Stable unique identifier for this instance.
		uint64 uniqueId = 0;

		/// @brief Mesh asset used for this instance.
		String meshName;

		/// @brief World-space position.
		Vector3 position;

		/// @brief World-space orientation.
		Quaternion rotation;

		/// @brief World-space scale.
		Vector3 scale = Vector3::UnitScale;

		/// @brief The terrain page this instance belongs to (used for streaming and persistence).
		uint16 pageIndex = 0;

		/// @brief Whether this instance participates in collision queries (movement, camera, etc.).
		bool collides = true;
	};

	/// @brief Renders large numbers of authored mesh instances (e.g. trees) using hardware instancing.
	/// @details Unlike the procedural Foliage system (grass), every instance here is explicitly placed,
	///          has a stable id and can be edited individually. Instances are bucketed into square cells
	///          (typically a few terrain tiles wide) and each (cell, mesh, submesh) combination is
	///          rendered as a single instanced draw call via a reused FoliageChunk. Editing an instance
	///          only rebuilds the affected cell, keeping authoring responsive.
	class InstancedFoliage final : public NonCopyable
	{
	public:
		/// @brief Creates a new instanced foliage system.
		/// @param scene The scene to add foliage to.
		/// @param device The graphics device for creating GPU resources.
		/// @param chunkSize Edge length of a render cell in world units. Should be larger than a
		///        terrain tile so that each cell batches enough instances to benefit from instancing.
		InstancedFoliage(Scene& scene, GraphicsDevice& device, float chunkSize);

		~InstancedFoliage() override;

	public:
		/// @brief Gets the edge length of a render cell in world units.
		[[nodiscard]] float GetChunkSize() const
		{
			return m_chunkSize;
		}

		/// @brief Sets whether newly built foliage chunks cast shadows.
		void SetCastShadows(bool castShadows);

		/// @brief Gets whether foliage chunks cast shadows.
		[[nodiscard]] bool GetCastShadows() const
		{
			return m_castShadows;
		}

		/// @brief Adds a new instance. The affected cell is marked dirty (call RebuildDirtyCells to render).
		/// @return False if an instance with the same id already exists.
		bool AddInstance(const InstancedFoliageInstance& instance);

		/// @brief Removes the instance with the given id, marking its cell dirty.
		/// @return False if no such instance exists.
		bool RemoveInstance(uint64 uniqueId);

		/// @brief Updates the transform of an existing instance, re-bucketing it if it crossed a cell border.
		/// @return False if no such instance exists.
		bool UpdateInstanceTransform(uint64 uniqueId, const Vector3& position, const Quaternion& rotation, const Vector3& scale);

		/// @brief Looks up an instance by id.
		[[nodiscard]] bool TryGetInstance(uint64 uniqueId, InstancedFoliageInstance& out) const;

		/// @brief Sets whether an existing instance participates in collision, marking its cell dirty.
		/// @return False if no such instance exists.
		bool SetInstanceCollides(uint64 uniqueId, bool collides);

		/// @brief Removes every instance belonging to the given terrain page (used when streaming pages out).
		void UnloadPage(uint16 pageIndex);

		/// @brief Removes all instances and chunks.
		void Clear();

		/// @brief Rebuilds GPU buffers for every cell changed since the last call. Cheap when nothing changed.
		void RebuildDirtyCells();

		/// @brief Finds the nearest instance hit by the given ray (bounding-box test).
		/// @param ray The ray in world space.
		/// @param outDistance Receives the hit distance along the ray.
		/// @return The id of the closest hit instance, or 0 if none was hit.
		[[nodiscard]] uint64 Raycast(const Ray& ray, float& outDistance) const;

		/// @brief Collects the ids of all instances whose position lies within radius (on the XZ plane) of center.
		void QueryInstancesInRadius(const Vector3& center, float radius, std::vector<uint64>& outIds) const;

		/// @brief Collects all instances that belong to the given terrain page (used for persistence).
		void GetInstancesForPage(uint16 pageIndex, std::vector<InstancedFoliageInstance>& out) const;

		/// @brief Collects the set of terrain pages that currently contain at least one instance.
		void GetUsedPages(std::vector<uint16>& out) const;

		/// @brief Gets the total number of instances across all cells.
		[[nodiscard]] size_t GetInstanceCount() const
		{
			return m_instances.size();
		}

		/// @brief Shows or hides all foliage chunks.
		void SetVisible(bool visible);

		/// @brief Gets whether foliage is currently visible.
		[[nodiscard]] bool IsVisible() const
		{
			return m_visible;
		}

	private:
		/// @brief Integer cell coordinate in cell space.
		struct CellCoord
		{
			int32 x;
			int32 z;

			bool operator<(const CellCoord& other) const
			{
				if (x != other.x)
				{
					return x < other.x;
				}
				return z < other.z;
			}

			bool operator==(const CellCoord& other) const
			{
				return x == other.x && z == other.z;
			}
		};

		struct Record
		{
			InstancedFoliageInstance instance;
			CellCoord cell;
		};

		struct ChunkEntry
		{
			FoliageChunkPtr chunk;
			SceneNode* node = nullptr;
		};

		struct CollisionEntry
		{
			std::shared_ptr<InstancedFoliageCollision> collision;
			SceneNode* node = nullptr;
		};

		struct Cell
		{
			SceneNode* node = nullptr;
			std::vector<ChunkEntry> chunks;
			std::vector<CollisionEntry> collisions;
			std::unordered_set<uint64> instanceIds;
		};

		[[nodiscard]] CellCoord ToCell(const Vector3& position) const;

		Cell& GetOrCreateCell(const CellCoord& coord);

		void DestroyCellChunks(Cell& cell);

		void RebuildCell(const CellCoord& coord, Cell& cell);

		FoliageLayerPtr GetOrCreateLayer(const String& meshName, const MeshPtr& mesh, uint16 submeshIndex);

		MeshPtr GetMesh(const String& meshName);

		static Matrix4 BuildTransform(const Vector3& position, const Quaternion& rotation, const Vector3& scale);

	private:
		Scene& m_scene;
		GraphicsDevice& m_device;
		SceneNode* m_rootNode = nullptr;
		float m_chunkSize;
		bool m_castShadows = true;
		bool m_visible = true;

		std::map<uint64, Record> m_instances;
		std::map<CellCoord, Cell> m_cells;
		std::set<CellCoord> m_dirtyCells;
		std::map<std::pair<String, uint16>, FoliageLayerPtr> m_layers;
		std::map<String, MeshPtr> m_meshCache;
	};
}
