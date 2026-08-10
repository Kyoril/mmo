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

		virtual ~Entity() override;

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

		/// @brief Returns the animation state set as a shared_ptr so callers can pin its
		///	lifetime across code that might replace it (e.g. a mesh swap mid-iteration).
		std::shared_ptr<AnimationStateSet> GetAllAnimationStatesShared() const { return m_animationStates; }

		void SetMesh(MeshPtr mesh);

		ICollidable* GetCollidable() override { return this; }

		const ICollidable* GetCollidable() const override { return this; }

	public:
		/// @copydoc MovableObject::SetCurrentCamera
		virtual void SetCurrentCamera(Camera& cam) override;
		
		virtual void PopulateRenderQueue(RenderQueue& renderQueue) override;

		void SetMaterial(const MaterialPtr& material);

		TagPoint* AttachObjectToBone(const String& boneName,
			MovableObject& pMovable,
			const Quaternion& offsetOrientation = Quaternion::Identity,
			const Vector3& offsetPosition = Vector3::Zero);

		MovableObject* DetachObjectFromBone(const String& movableName);

		void DetachObjectFromBone(const MovableObject& obj);

		void DetachAllObjectsFromBone();
	public:
		/// @brief True when the bone matrices are stale for the current animation-state frame.
		///        Frame-based caching ensures animations are only computed once per frame even
		///        though rendering runs multiple passes (shadow cascades, depth pre-pass, ...).
		[[nodiscard]] bool NeedsAnimationUpdate() const;

		/// @brief Main-thread step: primes the shared animation sampling caches and (re)creates
		///        the bone-matrix constant buffer. Must run before ComputeBoneMatrices.
		void PrepareAnimationSampling();

		/// @brief Evaluates the skeleton pose into the bone matrix array. Pure CPU — safe on a
		///        TaskSystem worker once PrepareAnimationSampling ran (see docs/threading.md).
		void ComputeBoneMatrices();

		/// @brief Main-thread step: uploads the computed bone matrices to the GPU.
		void UploadBoneMatrices();

		/// @brief Marks this entity as queued for the scene's batched animation pass.
		/// @return false if it was already queued (duplicate submissions must not run the
		///         same SkeletonInstance on two workers concurrently).
		bool TryMarkQueuedForAnimationUpdate()
		{
			if (m_queuedForAnimationUpdate)
			{
				return false;
			}
			m_queuedForAnimationUpdate = true;
			return true;
		}

		/// @brief Clears the queued mark after the batched animation pass processed this entity.
		void ClearQueuedForAnimationUpdate() { m_queuedForAnimationUpdate = false; }

		/// @brief Debug helper: re-evaluates the pose serially and compares it with the bone
		///        matrices currently stored (the parallel result). Deterministic math must
		///        match bit-for-bit; a mismatch indicates a data race in the parallel path.
		[[nodiscard]] bool VerifySerialBoneMatrices();

	protected:
		/// @brief Updates skeletal animations and bone matrices synchronously (prepare +
		///        compute + upload). Kept for callers outside the scene's batched pass.
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

		std::vector<Matrix4> m_boneMatrices;

		std::shared_ptr<SkeletonInstance> m_skeleton;

		std::shared_ptr<AnimationStateSet> m_animationStates{ nullptr };
		
		/// @brief Frame number when animations were last updated to prevent multiple updates per frame
		mutable uint64 m_lastAnimationUpdateFrame{ 0 };
		
		/// @brief Cached flag to check if animations need updating this frame
		mutable bool m_animationsNeedUpdate{ true };

		/// True while this entity sits in the scene's pending animation update batch.
		bool m_queuedForAnimationUpdate{ false };

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

		/// @brief Enables or disables this entity's collision without touching its query flags
		/// (query flags also drive mouse picking, which must stay unaffected). Used e.g. by
		/// doors, whose collision is only active while they are closed.
		void SetCollisionEnabled(const bool enabled) { m_collisionEnabled = enabled; }

		/// @brief Returns whether collision is enabled for this entity (see SetCollisionEnabled).
		bool IsCollisionEnabled() const { return m_collisionEnabled; }

		bool IsCollidable() const override { return m_collisionEnabled && m_mesh && !m_mesh->GetCollisionTree().IsEmpty(); }

		bool TestRayCollision(const Ray& ray, CollisionResult& result) const override;

		/// @copydoc ICollidable::GetSurfaceTypeAt
		[[nodiscard]] uint32 GetSurfaceTypeAt(const CollisionResult& hit) const override;

	private:
		ConstantBufferPtr m_boneMatrixBuffer;
		
		/// @brief Cached world-space bounding box for entities with transforms
		mutable AABB m_worldBounds{};

		/// @brief Flag to track if world bounds need recalculation
		mutable bool m_worldBoundsDirty{ true };

		/// @brief Whether this entity's mesh collision tree participates in collision queries.
		bool m_collisionEnabled{ true };
	};
}
