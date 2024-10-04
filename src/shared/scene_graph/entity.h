// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "sub_entity.h"
#include "mesh.h"
#include "base/linear_set.h"
#include "skeleton_instance.h"

#include "scene_graph/movable_object.h"

namespace mmo
{
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
		[[nodiscard]] const MeshPtr& GetMesh() const noexcept { return m_mesh; }

		[[nodiscard]] std::shared_ptr<SkeletonInstance> GetSkeleton() const noexcept { return m_skeleton; }

		[[nodiscard]] bool HasSkeleton() const noexcept { return m_skeleton != nullptr; }

		SubEntity* GetSubEntity(uint16 index) const noexcept;

		SubEntity* GetSubEntity(const String& name) const noexcept;

		uint32 GetNumSubEntities() const noexcept;

		AnimationState* GetAnimationState(const String& name) const;

		bool HasAnimationState(const String& name) const;

		AnimationStateSet* GetAllAnimationStates() const;

	public:
		/// @copydoc MovableObject::SetCurrentCamera
		virtual void SetCurrentCamera(Camera& cam) override;
		
		virtual void PopulateRenderQueue(RenderQueue& renderQueue) override;

		void SetMaterial(const std::shared_ptr<Material>& material);

		void SetUserObject(void* userObject) { m_userObject = userObject; }

		template<typename T>
		T* GetUserObject() const { return static_cast<T*>(m_userObject); }

	protected:
		void UpdateAnimations();

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
