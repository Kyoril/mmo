// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "material.h"
#include "graphics/vertex_buffer.h"
#include "graphics/index_buffer.h"

namespace mmo
{
	class RenderOperation;
	class Mesh;

	/// @brief This class is a renderable part of a mesh with it's own material id.
	class SubMesh
	{
	public:
		/// @brief Creates a new instance of the SubMesh class and initializes it.
		/// @param parent The Mesh parent that this sub mesh belongs to.
		SubMesh(Mesh& parent);

	public:
		/// @brief Used to manually render the sub mesh.
		void Render() const;

		/// @brief Prepares a render operation in order to be able to render the sub mesh.
		/// @param op The render operation to setup.
		void PrepareRenderOperation(RenderOperation& op) const;

		/// @brief Gets the material assigned to this sub mesh.
		[[nodiscard]] std::shared_ptr<Material>& GetMaterial() noexcept { return m_material; }

		/// @brief Sets the material used by this sub mesh.
		/// @param material The new material to use or nullptr if no material should be used.
		void SetMaterial(const std::shared_ptr<Material>& material);

	public:
		Mesh& m_parent;
		VertexBufferPtr m_vertexBuffer;
		IndexBufferPtr m_indexBuffer;
		std::shared_ptr<Material> m_material;
		bool m_useSharedVertices { true };
	};
}
