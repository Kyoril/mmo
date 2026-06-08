// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "instanced_foliage.h"

#include "scene.h"
#include "scene_node.h"
#include "mesh_manager.h"
#include "sub_mesh.h"
#include "graphics/graphics_device.h"
#include "math/ray.h"
#include "math/aabb.h"
#include "log/default_log_levels.h"

#include <cmath>
#include <limits>
#include <unordered_map>

namespace mmo
{
	InstancedFoliage::InstancedFoliage(Scene& scene, GraphicsDevice& device, const float chunkSize)
		: m_scene(scene)
		, m_device(device)
		, m_chunkSize(chunkSize > 0.0f ? chunkSize : 1.0f)
	{
		m_rootNode = &m_scene.CreateSceneNode("InstancedFoliageRoot");
		m_scene.GetRootSceneNode().AddChild(*m_rootNode);
	}

	InstancedFoliage::~InstancedFoliage()
	{
		Clear();

		if (m_rootNode)
		{
			m_scene.DestroySceneNode(*m_rootNode);
			m_rootNode = nullptr;
		}
	}

	void InstancedFoliage::SetCastShadows(const bool castShadows)
	{
		m_castShadows = castShadows;

		// Existing chunks read their shadow flag from the shared layer, so updating the layer
		// settings is enough to affect already-built chunks as well as future ones.
		for (auto& [key, layer] : m_layers)
		{
			layer->GetSettings().castShadows = castShadows;
		}
	}

	InstancedFoliage::CellCoord InstancedFoliage::ToCell(const Vector3& position) const
	{
		return CellCoord{
			static_cast<int32>(std::floor(position.x / m_chunkSize)),
			static_cast<int32>(std::floor(position.z / m_chunkSize))
		};
	}

	bool InstancedFoliage::AddInstance(const InstancedFoliageInstance& instance)
	{
		if (instance.uniqueId == 0 || m_instances.find(instance.uniqueId) != m_instances.end())
		{
			return false;
		}

		const CellCoord cell = ToCell(instance.position);

		Record record;
		record.instance = instance;
		record.cell = cell;
		m_instances.emplace(instance.uniqueId, std::move(record));

		Cell& cellData = GetOrCreateCell(cell);
		cellData.instanceIds.insert(instance.uniqueId);

		m_dirtyCells.insert(cell);
		return true;
	}

	bool InstancedFoliage::RemoveInstance(const uint64 uniqueId)
	{
		const auto it = m_instances.find(uniqueId);
		if (it == m_instances.end())
		{
			return false;
		}

		const CellCoord cell = it->second.cell;
		m_instances.erase(it);

		const auto cellIt = m_cells.find(cell);
		if (cellIt != m_cells.end())
		{
			cellIt->second.instanceIds.erase(uniqueId);
			m_dirtyCells.insert(cell);
		}

		return true;
	}

	bool InstancedFoliage::UpdateInstanceTransform(const uint64 uniqueId, const Vector3& position, const Quaternion& rotation, const Vector3& scale)
	{
		const auto it = m_instances.find(uniqueId);
		if (it == m_instances.end())
		{
			return false;
		}

		it->second.instance.position = position;
		it->second.instance.rotation = rotation;
		it->second.instance.scale = scale;

		const CellCoord oldCell = it->second.cell;
		const CellCoord newCell = ToCell(position);

		if (!(newCell == oldCell))
		{
			// The instance crossed a cell boundary - move it between cells.
			if (const auto oldIt = m_cells.find(oldCell); oldIt != m_cells.end())
			{
				oldIt->second.instanceIds.erase(uniqueId);
				m_dirtyCells.insert(oldCell);
			}

			it->second.cell = newCell;
			Cell& newCellData = GetOrCreateCell(newCell);
			newCellData.instanceIds.insert(uniqueId);
		}

		m_dirtyCells.insert(it->second.cell);
		return true;
	}

	bool InstancedFoliage::TryGetInstance(const uint64 uniqueId, InstancedFoliageInstance& out) const
	{
		const auto it = m_instances.find(uniqueId);
		if (it == m_instances.end())
		{
			return false;
		}

		out = it->second.instance;
		return true;
	}

	void InstancedFoliage::UnloadPage(const uint16 pageIndex)
	{
		std::vector<uint64> toRemove;
		for (const auto& [id, record] : m_instances)
		{
			if (record.instance.pageIndex == pageIndex)
			{
				toRemove.push_back(id);
			}
		}

		for (const uint64 id : toRemove)
		{
			RemoveInstance(id);
		}

		// Apply the removals immediately so the page's chunks are gone after streaming out.
		RebuildDirtyCells();
	}

	void InstancedFoliage::Clear()
	{
		for (auto& [coord, cell] : m_cells)
		{
			DestroyCellChunks(cell);
			if (cell.node)
			{
				m_scene.DestroySceneNode(*cell.node);
				cell.node = nullptr;
			}
		}

		m_cells.clear();
		m_instances.clear();
		m_dirtyCells.clear();
		m_layers.clear();
		m_meshCache.clear();
	}

	InstancedFoliage::Cell& InstancedFoliage::GetOrCreateCell(const CellCoord& coord)
	{
		const auto it = m_cells.find(coord);
		if (it != m_cells.end())
		{
			return it->second;
		}

		Cell cell;
		cell.node = m_rootNode->CreateChildSceneNode();
		return m_cells.emplace(coord, std::move(cell)).first->second;
	}

	void InstancedFoliage::DestroyCellChunks(Cell& cell)
	{
		for (auto& entry : cell.chunks)
		{
			if (entry.chunk)
			{
				entry.chunk->DetachFromParent();
			}
			if (entry.node)
			{
				m_scene.DestroySceneNode(*entry.node);
			}
		}
		cell.chunks.clear();
	}

	void InstancedFoliage::RebuildDirtyCells()
	{
		if (m_dirtyCells.empty())
		{
			return;
		}

		for (const CellCoord& coord : m_dirtyCells)
		{
			const auto it = m_cells.find(coord);
			if (it == m_cells.end())
			{
				continue;
			}

			Cell& cell = it->second;

			if (cell.instanceIds.empty())
			{
				// Cell no longer holds anything - tear it down completely.
				DestroyCellChunks(cell);
				if (cell.node)
				{
					m_scene.DestroySceneNode(*cell.node);
				}
				m_cells.erase(it);
				continue;
			}

			RebuildCell(coord, cell);
		}

		m_dirtyCells.clear();
	}

	void InstancedFoliage::RebuildCell(const CellCoord& coord, Cell& cell)
	{
		DestroyCellChunks(cell);

		if (!cell.node)
		{
			cell.node = m_rootNode->CreateChildSceneNode();
		}

		// Group this cell's instances by mesh so that each mesh can be batched on its own.
		std::unordered_map<String, std::vector<uint64>> byMesh;
		for (const uint64 id : cell.instanceIds)
		{
			const auto recordIt = m_instances.find(id);
			if (recordIt == m_instances.end())
			{
				continue;
			}
			byMesh[recordIt->second.instance.meshName].push_back(id);
		}

		for (const auto& [meshName, ids] : byMesh)
		{
			const MeshPtr mesh = GetMesh(meshName);
			if (!mesh || mesh->GetSubMeshCount() == 0)
			{
				continue;
			}

			const uint16 submeshCount = static_cast<uint16>(mesh->GetSubMeshCount());
			for (uint16 submeshIndex = 0; submeshIndex < submeshCount; ++submeshIndex)
			{
				const FoliageLayerPtr layer = GetOrCreateLayer(meshName, mesh, submeshIndex);

				auto chunk = std::make_shared<FoliageChunk>(layer, coord.x, coord.z, m_chunkSize);

				for (const uint64 id : ids)
				{
					const auto recordIt = m_instances.find(id);
					if (recordIt == m_instances.end())
					{
						continue;
					}

					const InstancedFoliageInstance& instance = recordIt->second.instance;
					FoliageInstanceData data;
					data.worldMatrix = BuildTransform(instance.position, instance.rotation, instance.scale);
					chunk->AddInstance(data);
				}

				chunk->BuildBuffers(m_device);

				SceneNode* chunkNode = cell.node->CreateChildSceneNode();
				chunkNode->AttachObject(*chunk);
				chunk->SetScene(&m_scene);
				chunk->SetVisible(m_visible);

				cell.chunks.push_back(ChunkEntry{ chunk, chunkNode });
			}
		}
	}

	FoliageLayerPtr InstancedFoliage::GetOrCreateLayer(const String& meshName, const MeshPtr& mesh, const uint16 submeshIndex)
	{
		const auto key = std::make_pair(meshName, submeshIndex);
		const auto it = m_layers.find(key);
		if (it != m_layers.end())
		{
			return it->second;
		}

		// One layer per (mesh, submesh). Material is left null so the chunk falls back to the
		// submesh's own material, which is what authored trees want (bark, canopy, etc.).
		auto layer = std::make_shared<FoliageLayer>(meshName + "#" + std::to_string(submeshIndex), mesh);
		layer->SetSubmeshIndex(submeshIndex);
		layer->GetSettings().castShadows = m_castShadows;

		m_layers.emplace(key, layer);
		return layer;
	}

	MeshPtr InstancedFoliage::GetMesh(const String& meshName)
	{
		const auto it = m_meshCache.find(meshName);
		if (it != m_meshCache.end())
		{
			return it->second;
		}

		MeshPtr mesh = MeshManager::Get().Load(meshName);
		if (!mesh)
		{
			WLOG("InstancedFoliage: failed to load mesh '" << meshName << "'");
		}

		m_meshCache.emplace(meshName, mesh);
		return mesh;
	}

	Matrix4 InstancedFoliage::BuildTransform(const Vector3& position, const Quaternion& rotation, const Vector3& scale)
	{
		Matrix4 transform;
		transform.MakeTransform(position, scale, rotation);
		return transform;
	}

	uint64 InstancedFoliage::Raycast(const Ray& ray, float& outDistance) const
	{
		uint64 bestId = 0;
		float bestT = std::numeric_limits<float>::max();

		for (const auto& [id, record] : m_instances)
		{
			const auto meshIt = m_meshCache.find(record.instance.meshName);
			MeshPtr mesh = meshIt != m_meshCache.end() ? meshIt->second : nullptr;
			if (!mesh)
			{
				// Avoid mutating the cache from a const method - load without caching here.
				mesh = MeshManager::Get().Load(record.instance.meshName);
			}
			if (!mesh)
			{
				continue;
			}

			AABB worldBounds = mesh->GetBounds();
			worldBounds.Transform(BuildTransform(record.instance.position, record.instance.rotation, record.instance.scale));

			const auto [hit, t] = ray.IntersectsAABB(worldBounds);
			if (hit && t >= 0.0f && t < bestT)
			{
				bestT = t;
				bestId = id;
			}
		}

		if (bestId != 0)
		{
			outDistance = bestT;
		}
		return bestId;
	}

	void InstancedFoliage::QueryInstancesInRadius(const Vector3& center, const float radius, std::vector<uint64>& outIds) const
	{
		const float radiusSq = radius * radius;
		for (const auto& [id, record] : m_instances)
		{
			const float dx = record.instance.position.x - center.x;
			const float dz = record.instance.position.z - center.z;
			if (dx * dx + dz * dz <= radiusSq)
			{
				outIds.push_back(id);
			}
		}
	}

	void InstancedFoliage::GetInstancesForPage(const uint16 pageIndex, std::vector<InstancedFoliageInstance>& out) const
	{
		for (const auto& [id, record] : m_instances)
		{
			if (record.instance.pageIndex == pageIndex)
			{
				out.push_back(record.instance);
			}
		}
	}

	void InstancedFoliage::GetUsedPages(std::vector<uint16>& out) const
	{
		std::set<uint16> pages;
		for (const auto& [id, record] : m_instances)
		{
			pages.insert(record.instance.pageIndex);
		}
		out.assign(pages.begin(), pages.end());
	}

	void InstancedFoliage::SetVisible(const bool visible)
	{
		if (m_visible == visible)
		{
			return;
		}

		m_visible = visible;
		for (auto& [coord, cell] : m_cells)
		{
			for (auto& entry : cell.chunks)
			{
				if (entry.chunk)
				{
					entry.chunk->SetVisible(visible);
				}
			}
		}
	}
}
