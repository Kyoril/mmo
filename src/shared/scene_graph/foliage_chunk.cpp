// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "foliage_chunk.h"
#include "foliage.h"
#include "camera.h"
#include "render_queue.h"
#include "render_operation.h"
#include "sub_mesh.h"
#include "graphics/graphics_device.h"
#include "log/default_log_levels.h"

namespace mmo
{
	String FoliageChunk::s_movableType = "FoliageChunk";

	FoliageChunk::FoliageChunk(
		Foliage& parent,
		const FoliageLayerPtr& layer,
		const int32 chunkX,
		const int32 chunkZ,
		const float chunkSize)
		: m_parent(parent)
		, m_layer(layer)
		, m_chunkX(chunkX)
		, m_chunkZ(chunkZ)
		, m_chunkSize(chunkSize)
	{
		// Calculate initial bounds
		const float halfSize = chunkSize * 0.5f;
		const float centerX = static_cast<float>(chunkX) * chunkSize + halfSize;
		const float centerZ = static_cast<float>(chunkZ) * chunkSize + halfSize;

		// Initial bounds - will be updated when instances are added
		m_bounds = AABB(
			Vector3(centerX - halfSize, -1000.0f, centerZ - halfSize),
			Vector3(centerX + halfSize, 1000.0f, centerZ + halfSize)
		);

		// Set shadow casting based on layer settings
		SetCastShadows(layer->GetSettings().castShadows);
	}

	Vector3 FoliageChunk::GetChunkCenter() const
	{
		const float halfSize = m_chunkSize * 0.5f;
		return Vector3(
			static_cast<float>(m_chunkX) * m_chunkSize + halfSize,
			m_bounds.GetCenter().y,
			static_cast<float>(m_chunkZ) * m_chunkSize + halfSize
		);
	}

	void FoliageChunk::ClearInstances()
	{
		m_instances.clear();
		m_needsRebuild = true;
	}

	void FoliageChunk::AddInstance(const FoliageInstanceData& instanceData)
	{
		m_instances.push_back(instanceData);
		m_needsRebuild = true;
	}

	void FoliageChunk::BuildBuffers(GraphicsDevice& device)
	{
		if (m_instances.empty())
		{
			m_instanceBuffer = nullptr;
			m_vertexData = nullptr;
			m_indexData = nullptr;
			m_needsRebuild = false;
			return;
		}

		// Get the mesh from the layer
		const auto& mesh = m_layer->GetMesh();
		if (!mesh || mesh->GetSubMeshCount() == 0)
		{
			m_needsRebuild = false;
			return;
		}

		// Clone vertex and index data from the first submesh
		const auto& subMesh = mesh->GetSubMesh(0);

		// Clone vertex data
		if (subMesh.useSharedVertices && mesh->sharedVertexData)
		{
			m_vertexData.reset(mesh->sharedVertexData->Clone(false, &device));
		}
		else if (subMesh.vertexData)
		{
			m_vertexData.reset(subMesh.vertexData->Clone(false, &device));
		}

		// Clone index data
		if (subMesh.indexData)
		{
			m_indexData.reset(subMesh.indexData->Clone(false));
		}

		// Create instance buffer with transform matrices
		const size_t bufferSize = m_instances.size() * sizeof(FoliageInstanceData);
		m_instanceBuffer = device.CreateVertexBuffer(
			m_instances.size(),
			sizeof(FoliageInstanceData),
			BufferUsage::Dynamic,
			m_instances.data()
		);

		// Create constant buffer for instance count and other per-chunk data
		struct ChunkConstants
		{
			uint32 instanceCount;
			float padding[3];
		};

		ChunkConstants constants;
		constants.instanceCount = static_cast<uint32>(m_instances.size());
		constants.padding[0] = constants.padding[1] = constants.padding[2] = 0.0f;

		m_instanceConstantBuffer = device.CreateConstantBuffer(sizeof(ChunkConstants), &constants);

		// Update bounds based on instances
		UpdateBounds();

		m_needsRebuild = false;
	}

	void FoliageChunk::UpdateBounds()
	{
		if (m_instances.empty())
		{
			return;
		}

		// Get mesh bounds
		const auto& mesh = m_layer->GetMesh();
		if (!mesh)
		{
			return;
		}

		const AABB& meshBounds = mesh->GetBounds();
		
		// Initialize with first instance bounds
		Vector3 minBounds(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
		Vector3 maxBounds(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());

		for (const auto& instance : m_instances)
		{
			// Transform mesh bounds by instance matrix
			const auto& worldMatrix = instance.worldMatrix;
			
			// Get the 8 corners of the mesh bounding box and transform them
			Vector3 corners[8];
			meshBounds.GetCorners(corners);

			for (const auto& corner : corners)
			{
				const Vector3 transformedCorner = worldMatrix.TransformAffine(corner);
				minBounds.x = std::min(minBounds.x, transformedCorner.x);
				minBounds.y = std::min(minBounds.y, transformedCorner.y);
				minBounds.z = std::min(minBounds.z, transformedCorner.z);
				maxBounds.x = std::max(maxBounds.x, transformedCorner.x);
				maxBounds.y = std::max(maxBounds.y, transformedCorner.y);
				maxBounds.z = std::max(maxBounds.z, transformedCorner.z);
			}
		}

		m_bounds = AABB(minBounds, maxBounds);
		m_boundingRadius = (maxBounds - minBounds).GetLength() * 0.5f;
	}

	const String& FoliageChunk::GetMovableType() const
	{
		return s_movableType;
	}

	const AABB& FoliageChunk::GetBoundingBox() const
	{
		return m_bounds;
	}

	float FoliageChunk::GetBoundingRadius() const
	{
		return m_boundingRadius;
	}

	void FoliageChunk::VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables)
	{
		if (HasInstances() && m_instanceBuffer)
		{
			visitor.Visit(*this, 0, false);
		}
	}

	void FoliageChunk::PopulateRenderQueue(RenderQueue& queue)
	{
		if (!HasInstances() || !m_instanceBuffer || !m_vertexData || !m_indexData)
		{
			return;
		}

		// Add to main render queue
		queue.AddRenderable(*this, Main);
	}

	void FoliageChunk::PrepareRenderOperation(RenderOperation& operation)
	{
		if (!m_vertexData || !m_indexData || !m_instanceBuffer)
		{
			return;
		}

		operation.topology = TopologyType::TriangleList;
		operation.vertexData = m_vertexData.get();
		operation.indexData = m_indexData.get();
		operation.material = GetMaterial();

		// Set up instanced rendering
		operation.instanceBuffer = m_instanceBuffer.get();
		operation.instanceCount = static_cast<uint32>(m_instances.size());
	}

	const Matrix4& FoliageChunk::GetWorldTransform() const
	{
		return Matrix4::Identity;
	}

	float FoliageChunk::GetSquaredViewDepth(const Camera& camera) const
	{
		const Vector3 cameraPos = camera.GetDerivedPosition();
		const Vector3 chunkCenter = GetChunkCenter();
		return (chunkCenter - cameraPos).GetSquaredLength();
	}

	bool FoliageChunk::GetCastsShadows() const
	{
		if (m_layer)
		{
			return m_layer->GetSettings().castShadows;
		}
		return false;
	}

	MaterialPtr FoliageChunk::GetMaterial() const
	{
		if (m_layer)
		{
			return m_layer->GetMaterial();
		}
		return nullptr;
	}
}
