// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "entity.h"

#include "animation_state.h"
#include "scene_graph/render_queue.h"
#include "scene_graph/scene_node.h"
#include "skeleton_instance.h"
#include "tag_point.h"

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

	void Entity::ResetSubEntities()
	{
		if (!m_mesh)
		{
			return;
		}

		uint16 index = 0;

		for (const auto& subEntity : m_subEntities)
		{
			subEntity->SetVisible(m_mesh->GetSubMesh(index).IsVisibleByDefault());
			subEntity->SetMaterial(m_mesh->GetSubMesh(index).GetMaterial());
			index++;
		}
	}

	SubEntity* Entity::GetSubEntity(const uint16 index) const
	{
		ASSERT(index <= m_subEntities.size());
		return m_subEntities[index].get();
	}

	SubEntity* Entity::GetSubEntity(const String& name) const
	{
		if (!m_mesh) return nullptr;

		const int32 index = m_mesh->GetSubMeshIndex(name);
		if (index < 0)
		{
			return nullptr;
		}

		return GetSubEntity(static_cast<uint16>(index));
	}

	uint32 Entity::GetNumSubEntities() const
	{
		return static_cast<uint32>(m_subEntities.size());
	}

	AnimationState* Entity::GetAnimationState(const String& name) const
	{
		if (!m_animationStates)
		{
			return nullptr;
		}

		return m_animationStates->GetAnimationState(name);
	}

	bool Entity::HasAnimationState(const String& name) const
	{
		if (!m_animationStates)
		{
			return false;
		}

		return m_animationStates->HasAnimationState(name);
	}

	AnimationStateSet* Entity::GetAllAnimationStates() const
	{
		return m_animationStates.get();
	}

	void Entity::SetMesh(MeshPtr mesh)
	{
		m_initialized = false;

		m_animationStates.reset();
		m_skeleton.reset();
		m_boneMatrices.clear();
		m_subEntities.clear();

		m_mesh = mesh;

		// Invalidate animation cache since we're changing the mesh/skeleton
		InvalidateAnimationCache();

		Initialize();
	}

	void Entity::PopulateRenderQueue(RenderQueue& renderQueue)
	{
		for (const auto& subEntity : m_subEntities)
		{
			if (!subEntity->IsVisible())
			{
				continue;
			}

			uint8 renderQueueGroup = GetRenderQueueGroup();
			if (subEntity->GetMaterial() && subEntity->GetMaterial()->IsTranslucent())
			{
				renderQueueGroup += 10;
			}

			renderQueue.AddRenderable(*subEntity, renderQueueGroup);
		}

		if (HasSkeleton())
		{
			UpdateAnimations();

			for (const auto& childIt : m_childObjects)
			{
				if (const bool visible = childIt.second->ShouldBeVisible())
				{
					childIt.second->PopulateRenderQueue(renderQueue);
				}
			}
		}
	}

	void Entity::SetMaterial(const MaterialPtr& material)
	{
		for (auto& subEntity : m_subEntities)
		{
			subEntity->SetMaterial(material);
		}
	}

	TagPoint* Entity::AttachObjectToBone(const String& boneName, MovableObject& pMovable, const Quaternion& offsetOrientation, const Vector3& offsetPosition)
	{
		ASSERT(!m_childObjects.contains(pMovable.GetName()));
		ASSERT(!pMovable.IsAttached());
		ASSERT(HasSkeleton());

		Bone* bone = m_skeleton->GetBone(boneName);
		ASSERT(bone);

		TagPoint* tp = m_skeleton->CreateTagPointOnBone(*bone, offsetOrientation, offsetPosition);
		tp->SetParentEntity(this);
		tp->SetChildObject(&pMovable);

		AttachObjectImpl(pMovable, *tp);

		// Trigger update of bounding box if necessary
		if (m_parentNode)
		{
			m_parentNode->NeedUpdate();
		}
		
		return tp;
	}

	MovableObject* Entity::DetachObjectFromBone(const String& movableName)
	{
		if (const auto it = m_childObjects.find(movableName); it != m_childObjects.end())
		{
			MovableObject* obj = it->second;
			DetachObjectImpl(*obj);
			m_childObjects.erase(it);

			if (m_parentNode)
			{
				m_parentNode->NeedUpdate();
			}

			return obj;
		}

		return nullptr;
	}

	void Entity::DetachObjectFromBone(const MovableObject& obj)
	{
		if (const auto it = m_childObjects.find(obj.GetName()); it != m_childObjects.end())
		{
			DetachObjectImpl(*it->second);
			m_childObjects.erase(it);

			if (m_parentNode)
			{
				m_parentNode->NeedUpdate();
			}
		}
	}

	void Entity::DetachAllObjectsFromBone()
	{
		DetachAllObjectsImpl();

		if (m_parentNode)
		{
			m_parentNode->NeedUpdate();
		}
	}
	
	void Entity::UpdateAnimations()
	{
		ASSERT(m_skeleton);
		ASSERT(m_animationStates);

		// Check if animations have been updated this frame already
		const uint64 currentAnimationFrame = m_animationStates->GetDirtyFrameNumber();
		if (m_lastAnimationUpdateFrame == currentAnimationFrame && !m_animationsNeedUpdate)
		{
			// Animations are already up to date for this frame
			return;
		}

		// Apply animation states
		m_skeleton->SetAnimationState(*m_animationStates);

		if (m_boneMatrices.size() != 256)
		{
			m_boneMatrices.resize(256, Matrix4::Identity);
			m_boneMatrixBuffer = GraphicsDevice::Get().CreateConstantBuffer(sizeof(Matrix4) * 256, m_boneMatrices.data());
		}

		m_skeleton->GetBoneMatrices(m_boneMatrices.data());
		m_boneMatrixBuffer->Update(m_boneMatrices.data());

		// Update cache information
		m_lastAnimationUpdateFrame = currentAnimationFrame;
		m_animationsNeedUpdate = false;
	}

	void Entity::AttachObjectImpl(MovableObject& pMovable, TagPoint& pAttachingPoint)
	{
		assert(!m_childObjects.contains(pMovable.GetName()));

		m_childObjects[pMovable.GetName()] = &pMovable;
		pMovable.NotifyAttachmentChanged(&pAttachingPoint, true);
	}

	void Entity::DetachObjectImpl(MovableObject& pObject) const
	{
		auto tp = static_cast<TagPoint*>(pObject.GetParentNode());

		// free the TagPoint so we can reuse it later
		m_skeleton->FreeTagPoint(*tp);
		pObject.NotifyAttachmentChanged(nullptr);
	}

	void Entity::DetachAllObjectsImpl()
	{
		for (auto& [name, obj] : m_childObjects)
		{
			DetachObjectImpl(*obj);
		}

		m_childObjects.clear();
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
        
        if (!m_mesh)
        {
            return;
        }

		m_boneMatrices.clear();

		BuildSubEntityList(m_mesh, m_subEntities);

		if (m_mesh->HasSkeleton())
		{
			m_skeleton = std::make_shared<SkeletonInstance>(m_mesh->GetSkeleton());
			m_animationStates = std::make_shared<AnimationStateSet>();
			m_skeleton->InitAnimationState(*m_animationStates);

			m_skeleton->Load();

			// Invalidate animation cache since we're initializing new animations
			InvalidateAnimationCache();
		}

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
		ASSERT(m_mesh);
		return m_mesh->GetBounds();
	}

	float Entity::GetBoundingRadius() const
	{
		ASSERT(m_mesh);
		return m_mesh->GetBoundRadius();
	}

	void Entity::VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables)
	{
		for(const auto& subEntity : m_subEntities)
		{
			visitor.Visit(*subEntity, 0, false);
		}
	}
}
