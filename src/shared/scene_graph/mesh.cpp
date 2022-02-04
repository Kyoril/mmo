// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "mesh.h"
#include "graphics/graphics_device.h"

#include <memory>


namespace mmo
{
	SubMesh & Mesh::CreateSubMesh()
	{
		auto subMesh = std::make_unique<SubMesh>(*this);
		m_subMeshes.emplace_back(std::move(subMesh));

		return *m_subMeshes.back();
	}

	SubMesh & Mesh::CreateSubMesh(const std::string & name)
	{
		auto& mesh = CreateSubMesh();
		NameSubMesh(m_subMeshes.size() - 1, name);

		return mesh;
	}

	void Mesh::NameSubMesh(const uint16 index, const std::string & name)
	{
		m_subMeshNames[name] = index;
	}

	SubMesh & Mesh::GetSubMesh(const uint16 index)
	{
		return *m_subMeshes[index];
	}

	SubMesh* Mesh::GetSubMesh(const std::string & name)
	{
		const auto index = m_subMeshNames.find(name);
		if (index != m_subMeshNames.end())
		{
			return &GetSubMesh(index->second);
		}

		return nullptr;
	}

	void Mesh::DestroySubMesh(const uint16 index)
	{
		// Get iterator and advance
		auto subMeshIt = m_subMeshes.begin();
		if (index > 0)
		{
			std::advance(subMeshIt, index);
		}

		// Remove sub mesh at the given index
		m_subMeshes.erase(subMeshIt);

		// Fix name index map
		for (auto it = m_subMeshNames.begin(); it != m_subMeshNames.end(); ++it)
		{
			if (it->second == index)
			{
				m_subMeshNames.erase(it);
				break;
			}
			
			if (it->second > index)
			{
				it->second--;
			}
		}
	}

	void Mesh::DestroySubMesh(const std::string & name)
	{
		const auto index = m_subMeshNames.find(name);
		if (index != m_subMeshNames.end())
		{
			DestroySubMesh(index->second);
			m_subMeshNames.erase(index);
		}
	}

	void Mesh::SetBounds(const AABB& bounds)
	{
		m_aabb = bounds;
		m_boundRadius = GetBoundingRadiusFromAABB(m_aabb);
	}
	
	void Mesh::Render()
	{
		// TODO: Eventually, these should be set by the sub meshes but for now,
		// this is what is supported by the mesh class.
		
		GraphicsDevice::Get().SetTopologyType(TopologyType::TriangleList); 
		GraphicsDevice::Get().SetVertexFormat(VertexFormat::PosColorNormalTex1);

		for (const auto& subMesh : m_subMeshes)
		{
			subMesh->Render();
		}
	}
}
