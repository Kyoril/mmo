// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "sub_mesh.h"
#include "scene_graph/renderable.h"

namespace mmo
{
	class Entity;
	class SubMesh;

	/// @brief A sub entity of an Entity which represents a certain part.
	class SubEntity : public Renderable
	{
	public:
		SubEntity(Entity& parent, SubMesh& subMesh);

	public:
		/// @brief Gets the parent entity that this sub entity belongs to.
		[[nodiscard]] Entity& GetParent() const noexcept { return m_parent; }

		/// @brief Gets the sub mesh assigned to this sub entity.
		[[nodiscard]] SubMesh& GetSubMesh() const noexcept { return m_subMesh; }
		
		/// @copydoc Renderable::PrepareRenderOperation
		virtual void PrepareRenderOperation(RenderOperation& operation) override;

		/// @copydoc Renderable::GetSquaredViewDepth
		[[nodiscard]] float GetSquaredViewDepth(const Camera& camera) const override;
		
		/// @copydoc Renderable::GetWorldTransform
		const Matrix4& GetWorldTransform() const override;

		/// @copydoc Renderable::GetMaterial
		[[nodiscard]] MaterialPtr GetMaterial() const override { return m_material ? m_material : m_subMesh.GetMaterial(); }

		/// @brief Sets the material to use when rendering this renderable.
		/// @param material The material to use for rendering or nullptr to use a default material.
		void SetMaterial(const MaterialPtr& material) noexcept { m_material = material; }

	private:
		Entity& m_parent;
		SubMesh& m_subMesh;
		bool m_visible { false };

		uint8 m_renderQueueId { 0 };
		bool m_renderQueueIdSet { false };
		uint16 m_renderQueuePriority { 0 };
		bool m_renderQueuePrioritySet { false };

		MaterialPtr m_material;

		mutable float m_cachedCameraDist { 0.0f };
		mutable const Camera* m_cachedCamera { nullptr };
	};
}
