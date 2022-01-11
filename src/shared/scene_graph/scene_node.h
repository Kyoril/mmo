// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "math/matrix4.h"
#include "math/quaternion.h"
#include "math/aabb.h"
#include "base/signal.h"
#include "base/typedefs.h"
#include <set>

#include "render_queue.h"


namespace mmo
{
	class Scene;
	class MovableObject;

	/// Enumerates available transform spaces.
	enum class TransformSpace : uint8
	{
		/// Operate in local space.
		Local,
		/// Operates relative to the parent, and only the parent.
		Parent,
		/// Operates relative to the world, which means relative to the absolute root node.
		World
	};
	
	/// This class is a node inside of a scene. Nodes can be used to group
	/// movable objects together and move them around in a scene.
	/// Each scene has exactly one root node, which can have one or multiple
	/// child nodes and/or attached movable objects to it.
	class SceneNode final
		: public NonCopyable
	{
		typedef std::unordered_map<String, SceneNode*> ChildNodeMap;

	public:
		// Child node iteration support
		using iterator = ChildNodeMap::iterator;
		using const_iterator = ChildNodeMap::const_iterator;

		iterator begin() { return m_children.begin(); }
		iterator end() { return m_children.end(); }
		const_iterator begin() const { return m_children.begin(); }
		const_iterator end() const { return m_children.end(); }
		const_iterator cbegin() const { return m_children.cbegin(); }
		const_iterator cend() const { return m_children.cend(); }
		size_t size() const { return m_children.size(); }
		bool IsInSceneGraph() const noexcept { return m_isInSceneGraph; }
		void RemoveAllChildren();
		void NotifyRootNode() noexcept { m_isInSceneGraph = true; }
		void FindVisibleObjects(Camera& camera, RenderQueue& renderQueue, VisibleObjectsBoundsInfo& visibleObjectBounds, bool includeChildren);
		void RemoveFromParent() const;

	public:
		/// Called when the node transformation has been updated.
		signal<void(const SceneNode&)> updated;
		/// Called when the node was attached to a parent scene node.
		signal<void(const SceneNode&)> nodeAttached;
		/// Called when the node was detached from a parent scene node.
		signal<void(const SceneNode&)> nodeDetached;

	public:
		explicit SceneNode(Scene& scene);
		explicit SceneNode(Scene& scene, String name);

	public:
		/// Gets the name of this node.
		const String& GetName() const { return m_name; }

		/// Gets the parent scene node.
		SceneNode* GetParent() const { return m_parent; }

		/// Updates the parent of this scene node.
		/// @param parent The new parent scene node or nullptr, if no new parent should be used.
		void SetParent(SceneNode* parent);
		
		/// Gets the derived orientation of the node.
		const Quaternion& GetDerivedOrientation();

		/// Gets the local orientation of the node.
		const Quaternion& GetOrientation() const { return m_orientation; }

		/// Sets the new local orientation of the node.
		void SetOrientation(const Quaternion& orientation);

		/// Sets the new absolute orientation of the node.
		void SetDerivedOrientation(const Quaternion& orientation);

		/// Rotates the node around the forward axis.
		void Roll(const Radian& angle, TransformSpace relativeTo);

		/// Rotates the node around the horizontal axis.
		void Pitch(const Radian& angle, TransformSpace relativeTo);

		/// Rotates the node around the vertical axis.
		void Yaw(const Radian& angle, TransformSpace relativeTo);

		/// Rotates the node on the given axis.
		void Rotate(const Vector3& axis, const Radian& angle, TransformSpace relativeTo);

		/// Rotates the node by using the given delta quaternion.
		void Rotate(const Quaternion& delta, TransformSpace relativeTo);

		/// Gets the derived scale of the node.
		const Vector3& GetDerivedScale();

		/// Gets the local scale of the node.
		const Vector3& GetScale() const { return m_scale; }

		/// Sets the scale value.
		void SetScale(const Vector3& scale);

		/// Scales the node. A value of Vector3::UnitScale won't do anything at all.
		void Scale(const Vector3& scaleOnAxis);

		/// Gets the derived position of the node.
		const Vector3& GetDerivedPosition();

		/// Gets the local position of the node.
		const Vector3& GetPosition() const { return m_position; }

		/// Updates the position of the node.
		void SetPosition(const Vector3& position);

		/// Sets the derived position.
		void SetDerivedPosition(const Vector3& position);

		/// Converts the given world position into a local position.
		Vector3 ConvertWorldToLocalPosition(const Vector3 &worldPos);

		/// Converts the given local position into a world position.
		Vector3 ConvertLocalToWorldPosition(const Vector3 &localPos);

		/// Converts the given local orientation into a world position.
		Quaternion ConvertWorldToLocalOrientation(const Quaternion &worldOrientation);

		/// Converts the given world orientation into a local position.
		Quaternion ConvertLocalToWorldOrientation(const Quaternion &localOrientation);

		/// Sets whether derived orientation will be inherited from parent.
		void SetInheritOrientation(bool inherit);

		/// Sets whether derived scale will be inherited from parent.
		void SetInheritScale(bool inherit);

		/// Gets whether derived orientation is inherited by the parent.
		bool IsInheritingOrientation() const { return m_inheritOrientation; }

		/// Gets whether derived scale is inherited by the parent.
		bool IsInheritingScale() const { return m_inheritScale; }
		
		/// Moves the position of this node.
		void Translate(const Vector3& delta, TransformSpace relativeTo);

		/// Adds a child scene node. The node must not have a parent already.
		void AddChild(SceneNode& child);
		
		/// Removes a child by name and returns it.
		/// @returns nullptr if a child with the given name could not be found.
		SceneNode* RemoveChild(const String& name);

		void Update(bool updateChildren, bool parentHasChanged);

		void UpdateBounds();

		const Matrix4& GetFullTransform();

		void AttachObject(MovableObject& obj);

		void DetachObject(MovableObject& object);
		
        void NeedUpdate(bool forceParentUpdate = false);
		
        void RequestUpdate(SceneNode& child, bool forceParentUpdate = false);
		
	protected:
		/// @brief Name of this node.
		String m_name;
		/// @brief The parent node.
		SceneNode* m_parent;
		/// @brief Flag to indicate own transform from parent is out of date.
		bool m_needParentUpdate;
		
		bool m_parentNotified;
		/// @brief Child nodes, mapped by name.
		ChildNodeMap m_children;
		/// @brief The local orientation of this node.
		Quaternion m_orientation;
		/// @brief The cached orientation of this node combined with it's parent node's world orientation.
		Quaternion m_derivedOrientation;
		/// @brief Whether this node inherits it's orientation from the parent node.
		bool m_inheritOrientation;
		/// @brief The local position of the node.
		Vector3 m_position;
		/// @brief The cached position of this node combined with it's parent node's world position.
		Vector3 m_derivedPosition;
		/// @brief The local scale of this node.
		Vector3 m_scale;
		/// @brief The cached scale of this node combined with it's parent node's world scale.
		Vector3 m_derivedScale;
		/// @brief Whether this node inherits it's scale from its parent node.
		bool m_inheritScale;
		/// @brief The cached transform matrix of this node.
		Matrix4 m_cachedTransform;
		/// @brief Whether the cached transform matrix is invalid and needs to be recalculated.
		bool m_cachedTransformInvalid;
		/// @brief Bounding box.
		AABB m_worldAABB;

		bool m_isInSceneGraph { false };

		typedef std::unordered_map<String, MovableObject*> ObjectMap;
		ObjectMap m_objectsByName;

		typedef std::set<SceneNode*> ChildUpdateSet;
		ChildUpdateSet m_childrenToUpdate;
		bool m_needChildUpdates = false;

	protected:
		void UpdateFromParent();
	};

}
