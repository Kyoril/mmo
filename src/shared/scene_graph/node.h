#pragma once

#include "base/typedefs.h"
#include "base/signal.h"

#include <unordered_map>
#include <set>

#include "math/matrix4.h"
#include "math/quaternion.h"

namespace mmo
{
	class Camera;

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

	class Node
	{
	public:

		typedef std::unordered_map<String, Node*> ChildNodeMap;

		// Child node iteration support
		using iterator = ChildNodeMap::iterator;
		using const_iterator = ChildNodeMap::const_iterator;

	public:
		iterator begin() { return m_children.begin(); }
		iterator end() { return m_children.end(); }
		const_iterator begin() const { return m_children.begin(); }
		const_iterator end() const { return m_children.end(); }
		const_iterator cbegin() const { return m_children.cbegin(); }
		const_iterator cend() const { return m_children.cend(); }
		size_t size() const { return m_children.size(); }

	public:
		/// Called when the node transformation has been updated.
		signal<void(const Node&)> updated;
		/// Called when the node was attached to a parent scene node.
		signal<void(const Node&)> nodeAttached;
		/// Called when the node was detached from a parent scene node.
		signal<void(const Node&)> nodeDetached;

	public:
		Node();
		Node(const String& name);
		virtual ~Node();

	public:
		/// Gets the name of this node.
		const String& GetName() const { return m_name; }

		/// Gets the parent scene node.
		virtual Node* GetParent() const { return m_parent; }

		/// Gets the local orientation of the node.
		virtual const Quaternion& GetOrientation() const { return m_orientation; }

		/// Sets the new local orientation of the node.
		virtual void SetOrientation(const Quaternion& orientation);

		virtual void ResetOrientation();

		/// Updates the position of the node.
		virtual void SetPosition(const Vector3& position);

		/// Gets the local position of the node.
		virtual const Vector3& GetPosition() const { return m_position; }

		/// Sets the scale value.
		virtual void SetScale(const Vector3& scale);

		/// Gets the local scale of the node.
		virtual const Vector3& GetScale() const { return m_scale; }

		/// Sets whether derived orientation will be inherited from parent.
		virtual void SetInheritOrientation(bool inherit);

		/// Gets whether derived orientation is inherited by the parent.
		virtual bool IsInheritingOrientation() const { return m_inheritOrientation; }

		/// Sets whether derived scale will be inherited from parent.
		virtual void SetInheritScale(bool inherit);

		/// Gets whether derived scale is inherited by the parent.
		virtual bool IsInheritingScale() const { return m_inheritScale; }

		/// Scales the node. A value of Vector3::UnitScale won't do anything at all.
		virtual void Scale(const Vector3& scaleOnAxis);

		/// Moves the position of this node.
		virtual void Translate(const Vector3& delta, TransformSpace relativeTo = TransformSpace::Parent);

		/// Rotates the node around the forward axis.
		virtual void Roll(const Radian& angle, TransformSpace relativeTo = TransformSpace::Local);

		/// Rotates the node around the horizontal axis.
		virtual void Pitch(const Radian& angle, TransformSpace relativeTo = TransformSpace::Local);

		/// Rotates the node around the vertical axis.
		virtual void Yaw(const Radian& angle, TransformSpace relativeTo = TransformSpace::Local);

		/// Rotates the node on the given axis.
		virtual void Rotate(const Vector3& axis, const Radian& angle, TransformSpace relativeTo = TransformSpace::Local);

		/// Rotates the node by using the given delta quaternion.
		virtual void Rotate(const Quaternion& delta, TransformSpace relativeTo = TransformSpace::Local);

		virtual Matrix3 GetLocalAxes() const;

		virtual Node* CreateChild(const Vector3& translate = Vector3::Zero, const Quaternion& rotate = Quaternion::Identity);

		virtual Node* CreateChild(const String& name, const Vector3& translate = Vector3::Zero, const Quaternion& rotate = Quaternion::Identity);

		/// Adds a child scene node. The node must not have a parent already.
		virtual void AddChild(Node& child);

		virtual uint32 GetNumChildren() const;

		virtual Node* GetChild(uint32 index) const;

		virtual Node* GetChild(const String& name) const;

		virtual Node* RemoveChild(uint32 index);

		virtual Node* RemoveChild(Node& child);

		virtual Node* RemoveChild(const String& name);

		virtual void RemoveAllChildren();

		/// Sets the derived position.
		virtual void SetDerivedPosition(const Vector3& position);

		/// Sets the new absolute orientation of the node.
		virtual void SetDerivedOrientation(const Quaternion& orientation);

		/// Gets the derived orientation of the node.
		virtual const Quaternion& GetDerivedOrientation() const;

		/// Gets the derived position of the node.
		virtual const Vector3& GetDerivedPosition() const;

		/// Gets the derived scale of the node.
		virtual const Vector3& GetDerivedScale() const;

		virtual const Matrix4& GetFullTransform() const;

		virtual void Update(bool updateChildren, bool parentHasChanged);

		virtual void SetInitialState();

		virtual void ResetToInitialState();

		virtual const Vector3& GetInitialPosition() const { return m_initialPosition; }

		virtual Vector3 ConvertWorldToLocalPosition(const Vector3& worldPos) const;

		virtual Vector3 ConvertLocalToWorldPosition(const Vector3& localPos) const;

		virtual Quaternion ConvertWorldToLocalOrientation(const Quaternion& worldOrientation) const;

		virtual Quaternion ConvertLocalToWorldOrientation(const Quaternion& localOrientation) const;

		virtual const Quaternion& GetInitialOrientation() const { return m_initialOrientation; }

		virtual const Vector3& GetInitialScale() const { return m_initialScale; }

		virtual float GetSquaredViewDepth(const Camera& camera) const;

		virtual void NeedUpdate(bool forceParentUpdate = false);

		virtual void RequestUpdate(Node& child, bool forceParentUpdate = false);

		virtual void CancelUpdate(Node& child);

		static void QueueNeedUpdate(Node& n);

		static void ProcessQueuedUpdates();

	protected:
		/// Updates the parent of this scene node.
		/// @param parent The new parent scene node or nullptr, if no new parent should be used.
		virtual void SetParent(Node* parent);

		virtual void UpdateFromParent() const;

		virtual void UpdateFromParentImpl() const;

		virtual Node* CreateChildImpl() = 0;

		virtual Node* CreateChildImpl(const String& name) = 0;

	protected:
		/// @brief The parent node.
		Node* m_parent;

		/// @brief Child nodes, mapped by name.
		ChildNodeMap m_children;

		typedef std::set<Node*> ChildUpdateSet;
		mutable ChildUpdateSet m_childrenToUpdate;

		/// @brief Flag to indicate own transform from parent is out of date.
		mutable bool m_needParentUpdate;

		mutable bool m_needChildUpdates = false;

		mutable bool m_parentNotified;

		mutable bool m_queuedForUpdate;

		/// @brief Name of this node.
		String m_name;

		/// @brief The local orientation of this node.
		Quaternion m_orientation;

		/// @brief The local position of the node.
		Vector3 m_position;

		/// @brief The local scale of this node.
		Vector3 m_scale;

		/// @brief Whether this node inherits it's orientation from the parent node.
		bool m_inheritOrientation;

		/// @brief Whether this node inherits it's scale from its parent node.
		bool m_inheritScale;

		/// @brief The cached orientation of this node combined with it's parent node's world orientation.
		mutable Quaternion m_derivedOrientation;

		/// @brief The cached position of this node combined with it's parent node's world position.
		mutable Vector3 m_derivedPosition;

		/// @brief The cached scale of this node combined with it's parent node's world scale.
		mutable Vector3 m_derivedScale;

		Vector3 m_initialPosition;

		Quaternion m_initialOrientation;

		Vector3 m_initialScale;

		/// @brief The cached transform matrix of this node.
		mutable Matrix4 m_cachedTransform;

		/// @brief Whether the cached transform matrix is invalid and needs to be recalculated.
		mutable bool m_cachedTransformInvalid;

		typedef std::vector<Node*> QueuedUpdates;
		static QueuedUpdates s_queuedUpdates;
	};
}
