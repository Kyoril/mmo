// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "mesh.h"
#include "mesh_instance_data.h"
#include "movable_object.h"
#include "renderable.h"
#include "graphics/material.h"
#include "graphics/vertex_buffer.h"
#include "math/aabb.h"

#include <vector>

namespace mmo
{
	class Camera;
	class GraphicsDevice;
	class RenderQueue;

	/// @brief Renders every placement of one submesh within a world model room as a single
	///        hardware-instanced draw call.
	/// @details Modular world models repeat the same module mesh dozens of times per room, which the
	///          per-mesh-reference Entity path turns into one draw call each. A batch collapses all
	///          placements that share a room, a mesh, a submesh and an effective material into one
	///          DrawIndexedInstanced.
	///
	///          Room is part of that key on purpose: portal culling toggles visibility per room, so
	///          batching across rooms would defeat it.
	///
	///          Geometry is *referenced*, never copied: PrepareRenderOperation points straight at the
	///          submesh's own VertexData/IndexData and the batch holds a MeshPtr to keep them alive.
	///          Cloning would be the obvious alternative (FoliageChunk does it), but VertexData's
	///          destructor cannot currently release the VertexDeclaration its constructor allocates,
	///          so every clone permanently leaks a declaration plus its input-layout cache. Sharing
	///          is also safe: instanced and non-instanced draws use separate input-layout caches on
	///          the declaration, so the same mesh can be drawn both ways in one frame.
	///
	///          Instance matrices are world-space. The generated instanced vertex shader uses them as
	///          the full world transform and ignores the per-object matWorld constant entirely, which
	///          is why GetWorldTransform returns identity and why the batch's scene node must never
	///          carry a transform - the bounds are already world-space and would be transformed twice.
	class WorldModelBatch final : public MovableObject, public Renderable
	{
	public:
		/// @brief Creates a batch for one submesh of one mesh.
		/// @param name Unique name for the movable object.
		/// @param mesh The mesh whose geometry every instance shares. Kept alive by this batch.
		/// @param submeshIndex Index of the submesh to draw.
		/// @param material The effective material: the mesh reference's override when it has one,
		///        otherwise the submesh's own material.
		WorldModelBatch(const String& name, MeshPtr mesh, uint16 submeshIndex, MaterialPtr material);

	public:
		/// @brief Removes all instances. The GPU buffer is only updated by UploadInstances.
		void ClearInstances();

		/// @brief Adds one placement.
		/// @param instance World transform and tint for this placement.
		void AddInstance(const MeshInstanceData& instance);

		/// @brief Gets the number of placements added so far.
		[[nodiscard]] size_t GetInstanceCount() const { return m_instances.size(); }

		/// @brief (Re)creates the instance buffer from the current placements and recomputes bounds.
		/// @remark Main thread only - it touches the graphics device. Must not be called from
		///         PrepareRenderOperation or PreRender; see WorldModelInstance::RefreshBatchTransforms
		///         for where this belongs in the frame.
		/// @param device The graphics device to create the buffer with.
		void UploadInstances(GraphicsDevice& device);

	public:
		// MovableObject interface

		/// @copydoc MovableObject::GetMovableType
		[[nodiscard]] const String& GetMovableType() const override;

		/// @copydoc MovableObject::GetBoundingBox
		[[nodiscard]] const AABB& GetBoundingBox() const override { return m_bounds; }

		/// @copydoc MovableObject::GetBoundingRadius
		[[nodiscard]] float GetBoundingRadius() const override { return m_boundingRadius; }

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
		[[nodiscard]] bool GetCastsShadows() const override { return IsCastingShadows(); }

		/// @copydoc Renderable::GetMaterial
		[[nodiscard]] MaterialPtr GetMaterial() const override { return m_material; }

	private:
		/// @brief Resolves the vertex data this batch draws from, honouring shared vertices.
		[[nodiscard]] VertexData* GetVertexData() const;

		/// @brief Resolves the index data this batch draws from.
		[[nodiscard]] IndexData* GetIndexData() const;

	private:
		MeshPtr m_mesh;
		uint16 m_submeshIndex;
		MaterialPtr m_material;

		/// @brief CPU-side placements, rebuilt whenever the owning world model moves.
		std::vector<MeshInstanceData> m_instances;

		/// @brief Per-instance vertex stream holding the data in m_instances.
		VertexBufferPtr m_instanceBuffer;

		/// @brief World-space union of the mesh bounds under every instance transform.
		AABB m_bounds;
		float m_boundingRadius { 0.0f };

		static String s_movableType;
	};

	typedef std::shared_ptr<WorldModelBatch> WorldModelBatchPtr;
}
