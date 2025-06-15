// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "sub_entity.h"
#include "mesh.h"
#include "base/linear_set.h"
#include "skeleton_instance.h"

#include "scene_graph/movable_object.h"

namespace mmo
{
	class TagPoint;
	class AnimationState;
	class RenderQueue;

	class Entity : public MovableObject
	{
		friend class SubEntity;

	public:

		typedef LinearSet<Entity*> EntitySet;

	public:
		Entity();

		Entity(const String& name, MeshPtr mesh);

		virtual ~Entity() override = default;

	public:
		void ResetSubEntities();

		[[nodiscard]] const MeshPtr& GetMesh() const { return m_mesh; }

		[[nodiscard]] std::shared_ptr<SkeletonInstance> GetSkeleton() const { return m_skeleton; }

		[[nodiscard]] bool HasSkeleton() const { return m_skeleton != nullptr; }

		SubEntity* GetSubEntity(uint16 index) const;

		SubEntity* GetSubEntity(const String& name) const;

		uint32 GetNumSubEntities() const;

		AnimationState* GetAnimationState(const String& name) const;

		bool HasAnimationState(const String& name) const;

		AnimationStateSet* GetAllAnimationStates() const;

		void SetMesh(MeshPtr mesh);

	public:
		/// @copydoc MovableObject::SetCurrentCamera
		virtual void SetCurrentCamera(Camera& cam) override;
		
		virtual void PopulateRenderQueue(RenderQueue& renderQueue) override;

		void SetMaterial(const MaterialPtr& material);

		void SetUserObject(void* userObject) { m_userObject = userObject; }

		template<typename T>
		T* GetUserObject() const { return static_cast<T*>(m_userObject); }

		TagPoint* AttachObjectToBone(const String& boneName,
			MovableObject& pMovable,
			const Quaternion& offsetOrientation = Quaternion::Identity,
			const Vector3& offsetPosition = Vector3::Zero);

		MovableObject* DetachObjectFromBone(const String& movableName);

		void DetachObjectFromBone(const MovableObject& obj);

		void DetachAllObjectsFromBone();

	protected:
		void UpdateAnimations();

		void AttachObjectImpl(MovableObject& pMovable, TagPoint& pAttachingPoint);

		void DetachObjectImpl(MovableObject& pObject) const;

		void DetachAllObjectsImpl();

	protected:
		MeshPtr m_mesh{nullptr};

		typedef std::vector<std::unique_ptr<SubEntity>> SubEntities;
		SubEntities m_subEntities{};

		bool m_initialized { false };

		Matrix4 m_lastParentTransform{};

		typedef std::map<String, MovableObject*> ChildObjects;
		ChildObjects m_childObjects{};

		mutable AABB m_fullBoundingBox {};

		void* m_userObject{ nullptr };

		std::vector<Matrix4> m_boneMatrices;

		std::shared_ptr<SkeletonInstance> m_skeleton;

		std::shared_ptr<AnimationStateSet> m_animationStates{ nullptr };
		
	protected:
		void BuildSubEntityList(const MeshPtr& mesh, SubEntities& subEntities);

		void Initialize();

		void DeInitialize();

	public:

		[[nodiscard]] const String& GetMovableType() const override;
		[[nodiscard]] const AABB& GetBoundingBox() const override;
		[[nodiscard]] float GetBoundingRadius() const override;
		void VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables) override;

	private:
		ConstantBufferPtr m_boneMatrixBuffer;
	};
}
