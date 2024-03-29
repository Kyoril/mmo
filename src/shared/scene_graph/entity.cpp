// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "entity.h"

#include "animation_state.h"
#include "scene_graph/render_queue.h"
#include "scene_graph/scene_node.h"
#include "skeleton_instance.h"

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

	AnimationState* Entity::GetAnimationState(const String& name) const
	{
		ASSERT(m_animationStates);
		return m_animationStates->GetAnimationState(name);
	}

	bool Entity::HasAnimationState(const String& name) const
	{
		ASSERT(m_animationStates);
		return m_animationStates->HasAnimationState(name);
	}

	AnimationStateSet* Entity::GetAllAnimationStates() const
	{
		return m_animationStates.get();
	}

	void Entity::PopulateRenderQueue(RenderQueue& renderQueue)
	{
		for (const auto& subEntity : m_subEntities)
		{
			renderQueue.AddRenderable(*subEntity, GetRenderQueueGroup());
		}

		if (HasSkeleton())
		{
			UpdateAnimations();
		}
	}

	void Entity::SetMaterial(const std::shared_ptr<Material>& material)
	{
		for (auto& subEntity : m_subEntities)
		{
			subEntity->SetMaterial(material);
		}
	}

	void Entity::UpdateAnimations()
	{
		// Move matrices into buffer
		ASSERT(m_skeleton);

		// Apply animation states
		m_skeleton->SetAnimationState(*m_animationStates);

		if (m_boneMatrices.size() != 128)
		{
			m_boneMatrices.resize(128);
			m_boneMatrixBuffer = GraphicsDevice::Get().CreateConstantBuffer(
				sizeof(Matrix4) * 128, m_boneMatrices.data());
		}

		m_skeleton->GetBoneMatrices(m_boneMatrices.data());
		m_boneMatrixBuffer->Update(m_boneMatrices.data());
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

		if (m_mesh->HasSkeleton())
		{
			m_skeleton = std::make_shared<SkeletonInstance>(m_mesh->GetSkeleton());
			m_animationStates = std::make_shared<AnimationStateSet>();
			m_mesh->InitAnimationState(*m_animationStates);

			m_skeleton->Load();
		}

		BuildSubEntityList(m_mesh, m_subEntities);

		if (m_parentNode)
		{
			m_parentNode->NeedUpdate();
		}

		m_initialized = true;
	}

	void Entity::DeInitialize()
	{
		if (!m_initialized)
		{
			return;
		}

		m_skeleton = nullptr;

		// TODO: Extra cleanup
		m_childObjects.clear();
		
		m_initialized = false;
	}

	const String& Entity::GetMovableType() const
	{
		static String EntityType = "Entity";
		return EntityType;
	}

	const AABB& Entity::GetBoundingBox() const
	{
		return m_mesh->GetBounds();
	}

	float Entity::GetBoundingRadius() const
	{
		return m_mesh->GetBoundRadius();
	}

	void Entity::VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables)
	{
		// TODO
	}
}
