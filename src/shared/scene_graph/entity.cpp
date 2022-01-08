
#include "entity.h"
#include "scene_graph/render_queue.h"
#include "scene_graph/scene_node.h"

namespace mmo
{
	Entity::Entity()
		: MovableObject()
	{
		TODO("Implement");
	}

	Entity::Entity(const String& name, MeshPtr mesh)
		: MovableObject(name)
		, m_mesh(std::move(mesh))
	{
		Initialize();
	}

	SubEntity* Entity::GetSubEntity(uint16 index) const noexcept
	{
		ASSERT(index <= m_subEntities.size());
		return m_subEntities[index].get();
	}

	SubEntity* Entity::GetSubEntity(const String& name) const noexcept
	{
		TODO("Implement");
		return nullptr;
	}

	uint32 Entity::GetNumSubEntities() const noexcept
	{
		return static_cast<uint32>(m_subEntities.size());
	}
	
	void Entity::UpdateRenderQueue(RenderQueue& renderQueue)
	{
		for (const auto& subEntity : m_subEntities)
		{
			renderQueue.AddRenderable(*subEntity);
		}
	}

	void Entity::SetCurrentCamera(Camera& cam)
	{
		MovableObject::SetCurrentCamera(cam);

		if (m_parentNode)
		{
			// TODO: LoD level computation
		}
	}

	void Entity::BuildSubEntityList(const MeshPtr& mesh, SubEntities& subEntities)
	{
		const uint16 numSubMeshes = mesh->GetSubMeshCount();

		for (uint16 i = 0; i < numSubMeshes; ++i)
		{
			auto& subMesh = m_mesh->GetSubMesh(i);
			subEntities.emplace_back(std::make_unique<SubEntity>(*this, subMesh));
		}
	}

	void Entity::Initialize()
	{
		if (m_initialized)
		{
			return;
		}

		BuildSubEntityList(m_mesh, m_subEntities);

		if (m_parentNode)
		{
			m_parentNode->NeedUpdate();
		}

		m_initialized = true;
	}

	void Entity::Deinitialize()
	{
		if (!m_initialized)
		{
			return;
		}

		// TODO: Extra cleanup
		m_childObjects.clear();
		
		m_initialized = false;
	}
}
