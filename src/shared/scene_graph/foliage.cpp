// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "foliage.h"
#include "camera.h"
#include "scene.h"
#include "scene_node.h"
#include "graphics/graphics_device.h"
#include "log/default_log_levels.h"

#include <cmath>

namespace mmo
{
	Foliage::Foliage(Scene& scene, GraphicsDevice& device)
		: m_scene(scene)
		, m_device(device)
	{
		// Create root node for foliage
		m_rootNode = &m_scene.CreateSceneNode("FoliageRoot");
		m_scene.GetRootSceneNode().AddChild(*m_rootNode);

		// Set default bounds (can be overridden)
		m_bounds = AABB(
			Vector3(-1000.0f, -100.0f, -1000.0f),
			Vector3(1000.0f, 500.0f, 1000.0f)
		);

		// Initialize camera position to force initial chunk loading
		m_lastCameraPosition = Vector3(std::numeric_limits<float>::max(), 0.0f, std::numeric_limits<float>::max());
	}

	Foliage::~Foliage()
	{
		Clear();

		// Remove root node from scene
		if (m_rootNode)
		{
			m_scene.DestroySceneNode(*m_rootNode);
			m_rootNode = nullptr;
		}
	}

	void Foliage::SetSettings(const FoliageSettings& settings)
	{
		const bool chunkSizeChanged = m_settings.chunkSize != settings.chunkSize;
		m_settings = settings;

		// If chunk size changed, we need to rebuild everything
		if (chunkSizeChanged)
		{
			m_needsRebuild = true;
		}
	}

	void Foliage::SetHeightQueryCallback(HeightQueryCallback callback)
	{
		m_heightQuery = std::move(callback);
	}

	void Foliage::AddLayer(const FoliageLayerPtr& layer)
	{
		if (!layer)
		{
			WLOG("Foliage::AddLayer - null layer provided");
			return;
		}

		// Check for duplicate names
		for (const auto& existing : m_layers)
		{
			if (existing->GetName() == layer->GetName())
			{
				WLOG("Foliage::AddLayer - duplicate layer name: " << layer->GetName());
				return;
			}
		}

		DLOG("Foliage::AddLayer - adding layer '" << layer->GetName() << "' with mesh: " << (layer->GetMesh() ? "yes" : "no") << ", material: " << (layer->GetMaterial() ? layer->GetMaterial()->GetName() : "none"));
		m_layers.push_back(layer);
		m_needsRebuild = true;
	}

	bool Foliage::RemoveLayer(const String& name)
	{
		for (auto it = m_layers.begin(); it != m_layers.end(); ++it)
		{
			if ((*it)->GetName() == name)
			{
				// Remove all chunks for this layer
				for (auto chunkIt = m_chunks.begin(); chunkIt != m_chunks.end();)
				{
					if (chunkIt->first.first == name)
					{
						// Detach from scene
						if (chunkIt->second->GetParentNode())
						{
							chunkIt->second->DetachFromParent();
						}
						chunkIt = m_chunks.erase(chunkIt);
					}
					else
					{
						++chunkIt;
					}
				}

				m_layers.erase(it);
				return true;
			}
		}

		return false;
	}

	FoliageLayerPtr Foliage::GetLayer(const String& name) const
	{
		for (const auto& layer : m_layers)
		{
			if (layer->GetName() == name)
			{
				return layer;
			}
		}
		return nullptr;
	}

	void Foliage::Clear()
	{
		// Detach all chunks from scene
		for (auto& [key, chunk] : m_chunks)
		{
			if (chunk->GetParentNode())
			{
				chunk->DetachFromParent();
			}
		}

		m_chunks.clear();
		m_activeChunks.clear();
		m_layers.clear();
	}

	void Foliage::Update(Camera& camera)
	{
		if (!m_visible || m_layers.empty())
		{
			return;
		}

		// Check if we need a full rebuild
		if (m_needsRebuild)
		{
			DLOG("Foliage::Update - rebuilding all chunks");
			RebuildAll();
			m_needsRebuild = false;
		}

		// Check if any layers are dirty
		for (const auto& layer : m_layers)
		{
			if (layer->IsDirty())
			{
				// Rebuild chunks for this layer
				for (auto& [key, chunk] : m_chunks)
				{
					if (key.first == layer->GetName())
					{
						chunk->MarkNeedsRebuild();
					}
				}
				layer->ClearDirty();
			}
		}

		// Update active chunks based on camera position
		UpdateActiveChunks(camera);

		// Build/rebuild any chunks that need it
		size_t chunksRebuilt = 0;
		for (auto& chunk : m_activeChunks)
		{
			if (chunk->NeedsRebuild())
			{
				const auto& layer = chunk->GetLayer();
				if (layer)
				{
					chunk->ClearInstances();
					GenerateChunkInstances(*chunk, *layer);
					chunk->BuildBuffers(m_device);
					chunksRebuilt++;
				}
			}
		}

		static bool firstUpdate = true;
		if (firstUpdate)
		{
			DLOG("Foliage::Update - active chunks: " << m_activeChunks.size() << ", total instances: " << GetTotalInstanceCount());
			firstUpdate = false;
		}
	}

	void Foliage::RebuildAll()
	{
		// Clear all existing chunks
		for (auto& [key, chunk] : m_chunks)
		{
			if (chunk->GetParentNode())
			{
				chunk->DetachFromParent();
			}
		}
		m_chunks.clear();
		m_activeChunks.clear();

		// Force update on next frame
		m_lastCameraPosition = Vector3(std::numeric_limits<float>::max(), 0.0f, std::numeric_limits<float>::max());
	}

	void Foliage::RebuildRegion(const AABB& region)
	{
		// Find chunks that intersect this region
		int32 minChunkX, minChunkZ, maxChunkX, maxChunkZ;
		WorldToChunk(region.min, minChunkX, minChunkZ);
		WorldToChunk(region.max, maxChunkX, maxChunkZ);

		for (auto& [key, chunk] : m_chunks)
		{
			const ChunkKey& chunkKey = key.second;
			if (chunkKey.x >= minChunkX && chunkKey.x <= maxChunkX &&
				chunkKey.z >= minChunkZ && chunkKey.z <= maxChunkZ)
			{
				chunk->MarkNeedsRebuild();
			}
		}
	}

	size_t Foliage::GetTotalInstanceCount() const
	{
		size_t total = 0;
		for (const auto& chunk : m_activeChunks)
		{
			total += chunk->GetInstanceCount();
		}
		return total;
	}

	void Foliage::SetVisible(const bool visible)
	{
		if (m_visible == visible)
		{
			return;
		}

		m_visible = visible;

		// Update visibility of all chunks
		for (auto& [key, chunk] : m_chunks)
		{
			chunk->SetVisible(visible);
		}
	}

	void Foliage::SetBounds(const AABB& bounds)
	{
		m_bounds = bounds;
		m_needsRebuild = true;
	}

	void Foliage::GenerateChunkInstances(FoliageChunk& chunk, const FoliageLayer& layer)
	{
		if (!m_heightQuery)
		{
			return;
		}

		const auto& settings = layer.GetSettings();
		const float chunkSize = m_settings.chunkSize;
		const float density = settings.density * m_settings.globalDensityMultiplier;

		if (density <= 0.0f)
		{
			return;
		}

		// Calculate world position of chunk
		const float chunkWorldX = static_cast<float>(chunk.GetChunkX()) * chunkSize;
		const float chunkWorldZ = static_cast<float>(chunk.GetChunkZ()) * chunkSize;

		// Check if chunk is within bounds
		const AABB chunkBounds(
			Vector3(chunkWorldX, m_bounds.min.y, chunkWorldZ),
			Vector3(chunkWorldX + chunkSize, m_bounds.max.y, chunkWorldZ + chunkSize)
		);

		if (!m_bounds.Intersects(chunkBounds))
		{
			return;
		}

		// Calculate number of instances based on density
		const float area = chunkSize * chunkSize;
		const int32 instanceCount = static_cast<int32>(area * density);

		if (instanceCount <= 0)
		{
			return;
		}

		// Setup random number generator
		const uint32 seed = settings.randomSeed != 0 
			? settings.randomSeed 
			: static_cast<uint32>(chunk.GetChunkX() * 73856093 ^ chunk.GetChunkZ() * 19349663);
		std::mt19937 rng(seed);
		std::uniform_real_distribution<float> posDist(0.0f, chunkSize);

		// Generate instances
		for (int32 i = 0; i < instanceCount; ++i)
		{
			// Random position within chunk
			const float localX = posDist(rng);
			const float localZ = posDist(rng);
			const float worldX = chunkWorldX + localX;
			const float worldZ = chunkWorldZ + localZ;

			// Query terrain height and normal
			float height = 0.0f;
			Vector3 normal(0.0f, 1.0f, 0.0f);
			
			if (!m_heightQuery(worldX, worldZ, height, normal))
			{
				continue;
			}

			// Calculate slope angle
			const float slopeAngle = std::acos(std::clamp(normal.y, -1.0f, 1.0f)) * (180.0f / 3.14159265f);

			// Check placement validity
			const Vector3 position(worldX, height, worldZ);
			if (!layer.IsValidPlacement(position, slopeAngle))
			{
				continue;
			}

			// Generate random scale and rotation
			const float scale = const_cast<FoliageLayer&>(layer).GenerateRandomScale(rng);
			const float yaw = const_cast<FoliageLayer&>(layer).GenerateRandomYaw(rng);

			// Build world transform matrix
			Matrix4 worldMatrix = Matrix4::Identity;

			// Apply scale
			Matrix4 scaleMatrix = Matrix4::Identity;
			scaleMatrix[0][0] = scale;
			scaleMatrix[1][1] = scale;
			scaleMatrix[2][2] = scale;

			// Apply rotation
			Matrix4 rotationMatrix = Matrix4::Identity;
			if (yaw != 0.0f)
			{
				const float cosYaw = std::cos(yaw);
				const float sinYaw = std::sin(yaw);
				rotationMatrix[0][0] = cosYaw;
				rotationMatrix[0][2] = sinYaw;
				rotationMatrix[2][0] = -sinYaw;
				rotationMatrix[2][2] = cosYaw;
			}

			// Apply normal alignment if enabled
			if (settings.alignToNormal && normal.y < 0.999f)
			{
				// Create rotation to align Y-up with surface normal
				const Vector3 up(0.0f, 1.0f, 0.0f);
				const Vector3 axis = up.Cross(normal);
				const float axisLen = axis.GetLength();

				if (axisLen > 0.001f)
				{
					const Vector3 axisNorm = axis / axisLen;
					const float angle = std::acos(std::clamp(up.Dot(normal), -1.0f, 1.0f));
					
					// Rodrigues' rotation formula
					const float c = std::cos(angle);
					const float s = std::sin(angle);
					const float t = 1.0f - c;
					
					Matrix4 alignMatrix = Matrix4::Identity;
					alignMatrix[0][0] = t * axisNorm.x * axisNorm.x + c;
					alignMatrix[0][1] = t * axisNorm.x * axisNorm.y - s * axisNorm.z;
					alignMatrix[0][2] = t * axisNorm.x * axisNorm.z + s * axisNorm.y;
					alignMatrix[1][0] = t * axisNorm.x * axisNorm.y + s * axisNorm.z;
					alignMatrix[1][1] = t * axisNorm.y * axisNorm.y + c;
					alignMatrix[1][2] = t * axisNorm.y * axisNorm.z - s * axisNorm.x;
					alignMatrix[2][0] = t * axisNorm.x * axisNorm.z - s * axisNorm.y;
					alignMatrix[2][1] = t * axisNorm.y * axisNorm.z + s * axisNorm.x;
					alignMatrix[2][2] = t * axisNorm.z * axisNorm.z + c;

					rotationMatrix = alignMatrix * rotationMatrix;
				}
			}

			// Apply translation
			Matrix4 translationMatrix = Matrix4::Identity;
			translationMatrix[0][3] = position.x;
			translationMatrix[1][3] = position.y;
			translationMatrix[2][3] = position.z;

			// Combine: Translation * Rotation * Scale
			worldMatrix = translationMatrix * rotationMatrix * scaleMatrix;

			// Add instance
			FoliageInstanceData instanceData;
			instanceData.worldMatrix = worldMatrix;
			chunk.AddInstance(instanceData);
		}
	}

	FoliageChunkPtr Foliage::GetOrCreateChunk(const int32 chunkX, const int32 chunkZ, const FoliageLayerPtr& layer)
	{
		const ChunkKey key{chunkX, chunkZ};
		const auto mapKey = std::make_pair(layer->GetName(), key);

		auto it = m_chunks.find(mapKey);
		if (it != m_chunks.end())
		{
			return it->second;
		}

		// Create new chunk
		auto chunk = std::make_shared<FoliageChunk>(
			*this,
			layer,
			chunkX,
			chunkZ,
			m_settings.chunkSize
		);

		// Create scene node for chunk
		const String nodeName = "FoliageChunk_" + layer->GetName() + "_" + 
			std::to_string(chunkX) + "_" + std::to_string(chunkZ);
		SceneNode& chunkNode = m_scene.CreateSceneNode(nodeName);
		m_rootNode->AddChild(chunkNode);
		chunkNode.AttachObject(*chunk);
		chunk->SetScene(&m_scene);

		m_chunks[mapKey] = chunk;
		return chunk;
	}

	void Foliage::WorldToChunk(const Vector3& worldPos, int32& chunkX, int32& chunkZ) const
	{
		chunkX = static_cast<int32>(std::floor(worldPos.x / m_settings.chunkSize));
		chunkZ = static_cast<int32>(std::floor(worldPos.z / m_settings.chunkSize));
	}

	void Foliage::UpdateActiveChunks(Camera& camera)
	{
		const Vector3 cameraPos = camera.GetDerivedPosition();

		// Get camera's chunk coordinates
		int32 cameraChunkX, cameraChunkZ;
		WorldToChunk(cameraPos, cameraChunkX, cameraChunkZ);

		// Check if camera has moved enough to warrant an update
		const float moveDist = (cameraPos - m_lastCameraPosition).GetLength();
		const float updateThreshold = m_settings.chunkSize * 0.25f;

		if (moveDist < updateThreshold && !m_activeChunks.empty())
		{
			return;
		}

		m_lastCameraPosition = cameraPos;

		// Clear current active chunks
		m_activeChunks.clear();

		// Load chunks within radius
		const int32 radius = m_settings.loadRadius;
		for (int32 dz = -radius; dz <= radius; ++dz)
		{
			for (int32 dx = -radius; dx <= radius; ++dx)
			{
				const int32 chunkX = cameraChunkX + dx;
				const int32 chunkZ = cameraChunkZ + dz;

				// Calculate distance from camera chunk
				const float dist = std::sqrt(static_cast<float>(dx * dx + dz * dz));
				if (dist > static_cast<float>(radius))
				{
					continue;
				}

				// Create/get chunks for all layers
				for (const auto& layer : m_layers)
				{
					auto chunk = GetOrCreateChunk(chunkX, chunkZ, layer);
					m_activeChunks.push_back(chunk);
				}
			}
		}

		// Unload distant chunks
		UnloadDistantChunks(cameraChunkX, cameraChunkZ);
	}

	void Foliage::UnloadDistantChunks(const int32 cameraChunkX, const int32 cameraChunkZ)
	{
		const int32 unloadRadius = m_settings.loadRadius + 2;
		
		std::vector<std::pair<String, ChunkKey>> toRemove;

		for (auto& [key, chunk] : m_chunks)
		{
			const ChunkKey& chunkKey = key.second;
			const int32 dx = chunkKey.x - cameraChunkX;
			const int32 dz = chunkKey.z - cameraChunkZ;
			const float dist = std::sqrt(static_cast<float>(dx * dx + dz * dz));

			if (dist > static_cast<float>(unloadRadius))
			{
				// Detach from scene
				if (chunk->GetParentNode())
				{
					SceneNode* node = chunk->GetParentSceneNode();
					chunk->DetachFromParent();
					if (node)
					{
						m_scene.DestroySceneNode(*node);
					}
				}
				toRemove.push_back(key);
			}
		}

		// Remove from map
		for (const auto& key : toRemove)
		{
			m_chunks.erase(key);
		}
	}
}
