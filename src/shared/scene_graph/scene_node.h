// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "node.h"
#include "math/matrix4.h"
#include "math/quaternion.h"
#include "math/aabb.h"
#include "base/typedefs.h"
#include "render_queue.h"

#include <set>


namespace mmo
{
	class Scene;
	class MovableObject;

	/// This class is a node inside of a scene. Nodes can be used to group
	/// movable objects together and move them around in a scene.
	/// Each scene has exactly one root node, which can have one or multiple
	/// child nodes and/or attached movable objects to it.
	class SceneNode final
		: public NonCopyable
		, public Node
	{
	public:
		explicit SceneNode(Scene& scene);
		explicit SceneNode(Scene& scene, const String& name);
		~SceneNode() override;

	public:

		void AttachObject(MovableObject& obj);

		uint32 GetNumAttachedObjects() const noexcept { return static_cast<uint32>(m_objectsByName.size()); }

		MovableObject* GetAttachedObject(uint32 index) const;

		MovableObject* GetAttachedObject(const String& name) const;

		void DetachObject(MovableObject& object);

		MovableObject* DetachObject(const String& name);

		void DetachAllObjects();

		bool IsInSceneGraph() const noexcept { return m_isInSceneGraph; }

		void NotifyRootNode() noexcept { m_isInSceneGraph = true; }

		void Update(bool updateChildren, bool parentHasChanged) override;

		void UpdateBounds();

		void RemoveAllChildren() override;

		void FindVisibleObjects(Camera& camera, RenderQueue& renderQueue, VisibleObjectsBoundsInfo& visibleObjectBounds, bool includeChildren);

		const AABB& GetWorldAABB() const noexcept { return m_worldAABB; }

		Scene& GetScene() const { return m_scene; }

		SceneNode* CreateChildSceneNode(const Vector3& translate = Vector3::Zero, const Quaternion& rotate = Quaternion::Identity);

		SceneNode* CreateChildSceneNode(const String& name, const Vector3& translate = Vector3::Zero, const Quaternion& rotate = Quaternion::Identity);

		void SetFixedYawAxis(bool useFixed, const Vector3& fixedAxis = Vector3::UnitY);
		
		void Yaw(const Radian& angle, TransformSpace relativeTo) override;

		void SetDirection(const Vector3& vec, TransformSpace relativeTo = TransformSpace::Local, const Vector3& localDirectionVector = Vector3::NegativeUnitZ);

		void LookAt(const Vector3& targetPoint, TransformSpace relativeTo, const Vector3& localDirectionVector = Vector3::NegativeUnitZ);

		void SetAutoTracking(bool enabled, SceneNode* const target = nullptr, const Vector3& localDirectionVector = Vector3::NegativeUnitZ, const Vector3& offset = Vector3::Zero);

		SceneNode* GetAutoTrackTarget() const { return m_autoTrackTarget; }

		const Vector3& GetAutoTrackOffset() const { return m_autoTrackOffset; }

		const Vector3& GetAutoTrackLocalDirection() const { return m_autoTrackLocalDirection; }

		void AutoTrack(void);

		SceneNode* GetParentSceneNode() const;

		void SetVisible(bool visible, bool cascade = true);

		void ToggleVisibility(bool cascade = true);

	public:
		void RemoveFromParent() const;

	protected:
		void UpdateFromParentImpl() const override;

		Node* CreateChildImpl() override;

		Node* CreateChildImpl(const String& name) override;

		void SetParent(Node* parent) override;

		void SetInSceneGraph(bool inSceneGraph);

	protected:
		typedef std::unordered_map<String, MovableObject*> ObjectMap;
		ObjectMap m_objectsByName{};

		Scene& m_scene;

		/// @brief Bounding box.
		AABB m_worldAABB;

		bool m_yawFixed { false };

		Vector3 m_yawFixedAxis{ Vector3::UnitY };

		SceneNode* m_autoTrackTarget { nullptr };

		Vector3 m_autoTrackOffset{ Vector3::Zero };

		Vector3 m_autoTrackLocalDirection { Vector3::Zero };

		bool m_isInSceneGraph { false };
	};

}
