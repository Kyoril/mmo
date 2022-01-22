// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "sub_mesh.h"

#include "base/typedefs.h"
#include "graphics/index_buffer.h"
#include "graphics/vertex_buffer.h"
#include "math/aabb.h"

#include <map>
#include <memory>
#include <vector>

namespace mmo
{
	class Mesh final
	{
        friend class SubMesh;
		friend class MeshSerializer;

	public:
		typedef std::vector<std::unique_ptr<SubMesh>> SubMeshList;
		typedef std::map<std::string, uint16> SubMeshNameMap;

	public:
		Mesh() = default;

	public: 
		SubMesh& CreateSubMesh();
		SubMesh& CreateSubMesh(const std::string& name);
		void NameSubMesh(uint16 index, const std::string& name);
		SubMesh& GetSubMesh(uint16 index);
		SubMesh* GetSubMesh(const std::string& name);
		void DestroySubMesh(uint16 index);
		void DestroySubMesh(const std::string& name);
		void SetBounds(const AABB& bounds);
		void Render();

		/// Determines whether this mesh has a link to a skeleton resource and thus supports animation.
		[[nodiscard]] bool HasSkeleton() const noexcept { return !m_skeletonName.empty(); }

	public:
		const SubMeshList& GetSubMeshes() const noexcept { return m_subMeshes; }
		uint16 GetSubMeshCount() const noexcept { return static_cast<uint16>(m_subMeshes.size()); }
		const AABB& GetBounds() const noexcept { return m_aabb; }
		float GetBoundRadius() const noexcept { return m_boundRadius; }

	public:
		VertexBufferPtr m_vertexBuffer;
		IndexBufferPtr m_indexBuffer;

	private:
		SubMeshList m_subMeshes;
		SubMeshNameMap m_subMeshNames;
		AABB m_aabb;
		float m_boundRadius { 0.0f };
		String m_skeletonName;
	};

	typedef std::shared_ptr<Mesh> MeshPtr;
}
