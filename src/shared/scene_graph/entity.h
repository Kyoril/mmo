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

	class Entity : public MovableObject, public ICollidable
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

		ICollidable* GetCollidable() override { return this; }

		const ICollidable* GetCollidable() const override { return this; }

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
		/// @brief Updates skeletal animations and bone matrices.
		/// This method is optimized to avoid redundant updates when called multiple times
		/// per frame (e.g., during shadow map and deferred rendering passes).
		/// Uses frame-based caching to ensure animations are only computed once per frame.
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
		
		/// @brief Frame number when animations were last updated to prevent multiple updates per frame
		mutable uint64 m_lastAnimationUpdateFrame{ 0 };
		
		/// @brief Cached flag to check if animations need updating this frame
		mutable bool m_animationsNeedUpdate{ true };

	public:

		/// @brief Invalidates the animation cache, forcing an update on next render
		void InvalidateAnimationCache() const 
		{ 
			m_animationsNeedUpdate = true; 
		}
		
	protected:
		void BuildSubEntityList(const MeshPtr& mesh, SubEntities& subEntities);

		void Initialize();

		void DeInitialize();

	public:

		[[nodiscard]] const String& GetMovableType() const override;
		[[nodiscard]] const AABB& GetBoundingBox() const override;
		[[nodiscard]] float GetBoundingRadius() const override;
		void VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables) override;

		bool TestCapsuleCollision(const Capsule& capsule, std::vector<CollisionResult>& results) const override;

		bool IsCollidable() const override { return m_mesh && !m_mesh->GetCollisionTree().IsEmpty(); }

		bool TestRayCollision(const Ray& ray, CollisionResult& result) const override;

	private:
		ConstantBufferPtr m_boneMatrixBuffer;
		
		/// @brief Cached world-space bounding box for entities with transforms
		mutable AABB m_worldBounds{};
		
		/// @brief Flag to track if world bounds need recalculation
		mutable bool m_worldBoundsDirty{ true };
	};
}
