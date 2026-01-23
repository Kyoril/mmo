// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "foliage_layer.h"
#include "renderable.h"
#include "movable_object.h"
#include "graphics/vertex_buffer.h"
#include "graphics/index_buffer.h"
#include "graphics/constant_buffer.h"
#include "math/aabb.h"
#include "math/matrix4.h"

#include <vector>

namespace mmo
{
	class Foliage;
	class Camera;
	class GraphicsDevice;
	class RenderQueue;

	/// @brief Represents a single foliage instance's transform data for GPU instancing.
	/// @details This structure is uploaded to the GPU as per-instance data.
	struct FoliageInstanceData
	{
		/// @brief World transform matrix for this instance (4x4 = 64 bytes).
		Matrix4 worldMatrix;
	};

	static_assert(sizeof(FoliageInstanceData) == 64, "FoliageInstanceData size mismatch");

	/// @brief A chunk of terrain that contains batched foliage instances.
	/// @details Each chunk manages a spatial region and renders all foliage instances
	///          within that region using GPU instancing for efficient rendering.
	class FoliageChunk final
		: public MovableObject
		, public Renderable
	{
	public:
		/// @brief Creates a new foliage chunk.
		/// @param parent The parent foliage system.
		/// @param layer The foliage layer this chunk renders.
		/// @param chunkX X coordinate of the chunk in chunk space.
		/// @param chunkZ Z coordinate of the chunk in chunk space.
		/// @param chunkSize Size of the chunk in world units.
		FoliageChunk(
			Foliage& parent,
			const FoliageLayerPtr& layer,
			int32 chunkX,
			int32 chunkZ,
			float chunkSize);

		~FoliageChunk() override = default;

	public:
		/// @brief Gets the X coordinate of this chunk.
		[[nodiscard]] int32 GetChunkX() const
		{
			return m_chunkX;
		}

		/// @brief Gets the Z coordinate of this chunk.
		[[nodiscard]] int32 GetChunkZ() const
		{
			return m_chunkZ;
		}

		/// @brief Gets the world position of the chunk's center.
		[[nodiscard]] Vector3 GetChunkCenter() const;

		/// @brief Gets the number of instances in this chunk.
		[[nodiscard]] size_t GetInstanceCount() const
		{
			return m_instances.size();
		}

		/// @brief Gets whether this chunk has any instances to render.
		[[nodiscard]] bool HasInstances() const
		{
			return !m_instances.empty();
		}

		/// @brief Clears all instances from this chunk.
		void ClearInstances();

		/// @brief Adds a foliage instance to this chunk.
		/// @param instanceData The instance transform data to add.
		void AddInstance(const FoliageInstanceData& instanceData);

		/// @brief Builds GPU buffers from the current instance data.
		/// @param device The graphics device to create buffers with.
		void BuildBuffers(GraphicsDevice& device);

		/// @brief Updates the bounding box based on current instances.
		void UpdateBounds();

		/// @brief Checks if buffers need to be rebuilt.
		[[nodiscard]] bool NeedsRebuild() const
		{
			return m_needsRebuild;
		}

		/// @brief Marks this chunk as needing a buffer rebuild.
		void MarkNeedsRebuild()
		{
			m_needsRebuild = true;
		}

		/// @brief Gets the parent foliage system.
		[[nodiscard]] Foliage& GetParent() const
		{
			return m_parent;
		}

		/// @brief Gets the foliage layer this chunk uses.
		[[nodiscard]] const FoliageLayerPtr& GetLayer() const
		{
			return m_layer;
		}

	public:
		// MovableObject interface
		
		/// @copydoc MovableObject::GetMovableType
		[[nodiscard]] const String& GetMovableType() const override;

		/// @copydoc MovableObject::GetBoundingBox
		[[nodiscard]] const AABB& GetBoundingBox() const override;

		/// @copydoc MovableObject::GetBoundingRadius
		[[nodiscard]] float GetBoundingRadius() const override;

		/// @copydoc MovableObject::VisitRenderables
		void VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables) override;

		/// @copydoc MovableObject::PopulateRenderQueue
		void PopulateRenderQueue(RenderQueue& queue) override;

	public:
		// Renderable interface

		/// @copydoc Renderable::PrepareRenderOperation
		void PrepareRenderOperation(RenderOperation& operation) override;

		/// @copydoc Renderable::GetWorldTransform
		[[nodiscard]] const Matrix4& GetWorldTransform() const override;

		/// @copydoc Renderable::GetSquaredViewDepth
		[[nodiscard]] float GetSquaredViewDepth(const Camera& camera) const override;

		/// @copydoc Renderable::GetCastsShadows
		[[nodiscard]] bool GetCastsShadows() const override;

		/// @copydoc Renderable::GetMaterial
		[[nodiscard]] MaterialPtr GetMaterial() const override;

	private:
		Foliage& m_parent;
		FoliageLayerPtr m_layer;
		int32 m_chunkX;
		int32 m_chunkZ;
		float m_chunkSize;
		AABB m_bounds;
		float m_boundingRadius = 0.0f;

		/// @brief Instance data for CPU-side storage.
		std::vector<FoliageInstanceData> m_instances;

		/// @brief GPU buffer containing instance transform matrices.
		VertexBufferPtr m_instanceBuffer;

		/// @brief Constant buffer for passing instance data to shaders.
		ConstantBufferPtr m_instanceConstantBuffer;

		/// @brief Whether buffers need to be rebuilt.
		bool m_needsRebuild = true;

		/// @brief Shared vertex data cloned from the mesh.
		std::unique_ptr<VertexData> m_vertexData;

		/// @brief Shared index data cloned from the mesh.
		std::unique_ptr<IndexData> m_indexData;

		/// @brief Static type name for MovableObject.
		static String s_movableType;
	};

	using FoliageChunkPtr = std::shared_ptr<FoliageChunk>;
}
