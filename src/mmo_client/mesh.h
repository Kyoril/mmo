// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#pragma once

#include "sub_mesh.h"

#include "base/typedefs.h"
#include "math/aabb.h"
#include "graphics/vertex_buffer.h"
#include "graphics/index_buffer.h"

#include <vector>
#include <memory>
#include <map>

namespace mmo
{
	class Mesh final
	{
	public:
		typedef std::vector<std::unique_ptr<SubMesh>> SubMeshList;
		typedef std::map<std::string, uint16> SubMeshNameMap;

	public:
		Mesh()
		{
		}

	public:
		SubMesh& CreateSubMesh();
		SubMesh& CreateSubMesh(const std::string& name);
		void NameSubMesh(uint16 index, const std::string& name);
		SubMesh& GetSubMesh(uint16 index);
		SubMesh* GetSubMesh(const std::string& name);
		void DestroySubMesh(uint16 index);
		void DestroySubMesh(const std::string& name);
		void Render();

	public:
		inline const SubMeshList& GetSubMeshes() const noexcept { return m_subMeshes; }
		inline uint16 GetSubMeshCount() const noexcept { return static_cast<uint16>(m_subMeshes.size()); }
		inline const AABB& GetBounds() const noexcept { return m_aabb; }
		inline float GetBoundRadius() const noexcept { return m_boundRadius; }

	private:
		SubMeshList m_subMeshes;
		SubMeshNameMap m_subMeshNames;
		AABB m_aabb;
		float m_boundRadius;
		VertexBufferPtr m_vertexBuffer;
		IndexBufferPtr m_indexBuffer;
	};

	typedef std::shared_ptr<Mesh> MeshPtr;
}
