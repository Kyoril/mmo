
#include "node.h"

#include "camera.h"

namespace mmo
{
	static uint32 s_nodeIndex = 0;

	Node::QueuedUpdates Node::s_queuedUpdates;

	Node::Node()
		: Node("Node_" + std::to_string(s_nodeIndex++))
	{
	}

	Node::Node(const String& name)
		: m_parent(nullptr)
		  , m_needParentUpdate(false)
		  , m_parentNotified(false)
	      , m_queuedForUpdate(false)
		  , m_name(name)
		  , m_orientation(Quaternion::Identity)
		  , m_position(Vector3::Zero)
		  , m_scale(Vector3::UnitScale)
		  , m_inheritOrientation(true)
		  , m_inheritScale(true)
		  , m_derivedOrientation(Quaternion::Identity)
		  , m_derivedPosition(Vector3::Zero)
		  , m_derivedScale(Vector3::UnitScale)
		  , m_initialPosition(Vector3::Zero)
		  , m_initialOrientation(Quaternion::Identity)
		  , m_initialScale(Vector3::UnitScale)
		  , m_cachedTransformInvalid(true)
	{
		NeedUpdate();
	}

	Node::~Node()
	{
		RemoveAllChildren();

		if (m_parent)
		{
			m_parent->RemoveChild(*this);
		}

		if (m_queuedForUpdate)
		{
			const auto it = std::find(s_queuedUpdates.begin(), s_queuedUpdates.end(), this);
			if (it != s_queuedUpdates.end())
			{
				*it = s_queuedUpdates.back();
				s_queuedUpdates.pop_back();
			}
		}
	}

	void Node::SetOrientation(const Quaternion& orientation)
	{
		ASSERT(!orientation.IsNaN());
		m_orientation = orientation;
		m_orientation.Normalize();
		NeedUpdate();
	}

	void Node::ResetOrientation()
	{
		m_orientation = Quaternion::Identity;
		NeedUpdate();
	}

	void Node::SetPosition(const Vector3& position)
	{
		m_position = position;
		NeedUpdate();
	}

	void Node::SetScale(const Vector3& scale)
	{
		m_scale = scale;
		NeedUpdate();
	}

	void Node::SetInheritOrientation(bool inherit)
	{
		m_inheritOrientation = inherit;
		NeedUpdate();
	}

	void Node::SetInheritScale(bool inherit)
	{
		m_inheritScale = inherit;
		NeedUpdate();
	}

	void Node::Scale(const Vector3& scaleOnAxis)
	{
		m_scale = scaleOnAxis * m_scale;
		NeedUpdate();
	}

	void Node::Translate(const Vector3& delta, TransformSpace relativeTo)
	{
		switch (relativeTo)
		{
		case TransformSpace::Local:
			m_position += m_orientation * delta;
			break;
		case TransformSpace::World:
			if (m_parent)
			{
				m_position += m_parent->GetDerivedOrientation().Inverse() * delta / m_parent->GetDerivedScale();
			}
			else
			{
				m_position += delta;
			}
			break;
		case TransformSpace::Parent:
			m_position += delta;
			break;
		}

		NeedUpdate();
	}

	void Node::Roll(const Radian& angle, TransformSpace relativeTo)
	{
		Rotate(Vector3::UnitZ, angle, relativeTo);
	}

	void Node::Pitch(const Radian& angle, TransformSpace relativeTo)
	{
		Rotate(Vector3::UnitX, angle, relativeTo);
	}

	void Node::Yaw(const Radian& angle, TransformSpace relativeTo)
	{
		Rotate(Vector3::UnitY, angle, relativeTo);
	}

	void Node::Rotate(const Vector3& axis, const Radian& angle, TransformSpace relativeTo)
	{
		Quaternion q;
		q.FromAngleAxis(angle, axis);
		Rotate(q, relativeTo);
	}

	void Node::Rotate(const Quaternion& delta, TransformSpace relativeTo)
	{
		Quaternion norm = delta;
		norm.Normalize();

		switch (relativeTo)
		{
		case TransformSpace::Parent:
			m_orientation = norm * m_orientation;
			break;
		case TransformSpace::World:
			m_orientation = m_orientation * GetDerivedOrientation().Inverse() * norm * GetDerivedOrientation();
			break;
		case TransformSpace::Local:
			m_orientation = m_orientation * norm;
			break;
		}

		NeedUpdate();
	}

	Matrix3 Node::GetLocalAxes() const
	{
		Vector3 axisX = Vector3::UnitX;
		Vector3 axisY = Vector3::UnitY;
		Vector3 axisZ = Vector3::UnitZ;

		axisX = m_orientation * axisX;
		axisY = m_orientation * axisY;
		axisZ = m_orientation * axisZ;

		return {
			axisX.x, axisY.x, axisZ.x,
			axisX.y, axisY.y, axisZ.y,
			axisX.z, axisY.z, axisZ.z
		};
	}

	Node* Node::CreateChild(const Vector3& translate, const Quaternion& rotate)
	{
		Node* newNode = CreateChildImpl();
		newNode->Translate(translate);
		newNode->Rotate(rotate);
		AddChild(*newNode);

		return newNode;
	}

	Node* Node::CreateChild(const String& name, const Vector3& translate, const Quaternion& rotate)
	{
		Node* newNode = CreateChildImpl(name);
		newNode->Translate(translate);
		newNode->Rotate(rotate);
		AddChild(*newNode);

		return newNode;
	}

	void Node::AddChild(Node& child)
	{
		ASSERT(!child.m_parent);

		m_children.emplace(ChildNodeMap::value_type(child.GetName(), &child));
		child.SetParent(this);
	}

	uint32 Node::GetNumChildren() const
	{
		return static_cast<uint32>(m_children.size());
	}

	Node* Node::GetChild(uint32 index) const
	{
		if (index < m_children.size())
		{
			auto i = m_children.begin();
			while (index--) ++i;
			return i->second;
		}

		return nullptr;
	}

	Node* Node::GetChild(const String& name) const
	{
		const auto it = m_children.find(name);
		if (it != m_children.end())
		{
			return it->second;
		}

		return nullptr;
	}

	Node* Node::RemoveChild(uint32 index)
	{
		ASSERT(index < m_children.size());

		auto i = m_children.begin();
		while (index--) ++i;

		Node* ret = i->second;
		CancelUpdate(*ret);

		m_children.erase(i);
		ret->SetParent(nullptr);
		return ret;
	}

	Node* Node::RemoveChild(Node& child)
	{
		const auto it = m_children.find(child.GetName());
		if (it != m_children.end() && it->second == &child)
		{
			CancelUpdate(child);

			m_children.erase(it);
			child.SetParent(nullptr);
		}

		return &child;
	}

	Node* Node::RemoveChild(const String& name)
	{
		const auto it = m_children.find(name);
		if (it == m_children.end())
		{
			return nullptr;
		}

		Node* node = it->second;
		CancelUpdate(*node);

		m_children.erase(it);
		node->SetParent(nullptr);
		return node;
	}

	void Node::RemoveAllChildren()
	{
		for (const auto& child : m_children)
		{
			child.second->SetParent(nullptr);
		}

		m_children.clear();
		m_childrenToUpdate.clear();
	}

	void Node::SetDerivedPosition(const Vector3& position)
	{
		if (m_parent)
		{
			SetPosition(m_parent->ConvertWorldToLocalPosition(position));
		}
	}

	void Node::SetDerivedOrientation(const Quaternion& orientation)
	{
		if (m_parent)
		{
			SetOrientation(m_parent->ConvertWorldToLocalOrientation(orientation));
		}
	}

	const Quaternion& Node::GetDerivedOrientation() const
	{
		if (m_needParentUpdate)
		{
			UpdateFromParent();
		}

		return m_derivedOrientation;
	}

	const Vector3& Node::GetDerivedPosition() const
	{
		if (m_needParentUpdate)
		{
			UpdateFromParent();
		}

		return m_derivedPosition;
	}

	const Vector3& Node::GetDerivedScale() const
	{
		if (m_needParentUpdate)
		{
			UpdateFromParent();
		}

		return m_derivedScale;
	}

	const Matrix4& Node::GetFullTransform() const
	{
		if (m_cachedTransformInvalid)
		{
			m_cachedTransform.MakeTransform(
				GetDerivedPosition(),
				GetDerivedScale(),
				GetDerivedOrientation()
			);
			m_cachedTransformInvalid = false;
		}

		return m_cachedTransform;
	}

	void Node::Update(bool updateChildren, bool parentHasChanged)
	{
		m_parentNotified = false;

		if (m_needParentUpdate || parentHasChanged)
		{
			UpdateFromParent();
		}

		if (updateChildren)
		{
			if (m_needChildUpdates || parentHasChanged)
			{
				for (const auto& [name, child] : m_children)
				{
					child->Update(true, true);
				}
			}
			else
			{
				for (auto& child : m_childrenToUpdate)
				{
					child->Update(true, false);
				}
			}

			m_childrenToUpdate.clear();
			m_needChildUpdates = false;
		}
	}

	void Node::SetInitialState()
	{
		m_initialPosition = m_position;
		m_initialOrientation = m_orientation;
		m_initialScale = m_scale;
	}

	void Node::ResetToInitialState()
	{
		m_position = m_initialPosition;
		m_orientation = m_initialOrientation;
		m_scale = m_initialScale;
		NeedUpdate();
	}

	Vector3 Node::ConvertWorldToLocalPosition(const Vector3& worldPos) const
	{
		if (m_needParentUpdate)
		{
			UpdateFromParent();
		}

		return m_derivedOrientation.Inverse() * (worldPos - m_derivedPosition) / m_derivedScale;
	}

	Vector3 Node::ConvertLocalToWorldPosition(const Vector3& localPos) const
	{
		if (m_needParentUpdate)
		{
			UpdateFromParent();
		}

		return m_derivedOrientation * (localPos * m_derivedScale) + m_derivedPosition;
	}

	Quaternion Node::ConvertWorldToLocalOrientation(const Quaternion& worldOrientation) const
	{
		if (m_needParentUpdate)
		{
			UpdateFromParent();
		}

		return m_derivedOrientation.Inverse() * worldOrientation;
	}

	Quaternion Node::ConvertLocalToWorldOrientation(const Quaternion& localOrientation) const
	{
		if (m_needParentUpdate)
		{
			UpdateFromParent();
		}

		return m_derivedOrientation * localOrientation;
	}

	float Node::GetSquaredViewDepth(const Camera& camera) const
	{
		const Vector3 diff = GetDerivedPosition() - camera.GetDerivedPosition();
		return diff.GetSquaredLength();
	}

	void Node::NeedUpdate(bool forceParentUpdate)
	{
		m_needParentUpdate = true;
		m_needChildUpdates = true;
		m_cachedTransformInvalid = true;

		// Make sure we're not root and parent hasn't been notified before
		if (m_parent && (!m_parentNotified || forceParentUpdate))
		{
			m_parent->RequestUpdate(*this, forceParentUpdate);
			m_parentNotified = true;
		}

		m_childrenToUpdate.clear();
	}

	void Node::RequestUpdate(Node& child, bool forceParentUpdate)
	{
		// If we're already going to update everything this doesn't matter
		if (m_needChildUpdates)
		{
			return;
		}

		m_childrenToUpdate.insert(&child);

		// Request selective update of me, if we didn't do it before
		if (m_parent && (!m_parentNotified || forceParentUpdate))
		{
			m_parent->RequestUpdate(*this, forceParentUpdate);
			m_parentNotified = true;
		}
	}

	void Node::CancelUpdate(Node& child)
	{
		m_childrenToUpdate.erase(&child);

		if (m_childrenToUpdate.empty() && m_parent && !m_needChildUpdates)
		{
			m_parent->CancelUpdate(*this);
			m_parentNotified = false;
		}
	}

	void Node::QueueNeedUpdate(Node& n)
	{
		if (!n.m_queuedForUpdate)
		{
			n.m_queuedForUpdate = true;
			s_queuedUpdates.push_back(&n);
		}
	}

	void Node::ProcessQueuedUpdates()
	{
		for (auto& node : s_queuedUpdates)
		{
			node->m_queuedForUpdate = false;
			node->NeedUpdate(true);
		}

		s_queuedUpdates.clear();
	}

	void Node::SetParent(Node* parent)
	{
		const bool newParent = parent != m_parent;

		m_parent = parent;
		m_parentNotified = false;
		NeedUpdate();

		if (newParent)
		{
			if (m_parent)
			{
				nodeAttached(*this);
			}
			else
			{
				nodeDetached(*this);
			}
		}
	}

	void Node::UpdateFromParent() const
	{
		UpdateFromParentImpl();
		updated(*this);
	}

	void Node::UpdateFromParentImpl() const
	{
		if (m_parent)
		{
			const Quaternion& parentOrientation = m_parent->GetDerivedOrientation();
			if (m_inheritOrientation)
			{
				m_derivedOrientation = parentOrientation * m_orientation;
			}
			else
			{
				m_derivedOrientation = m_orientation;
			}

			const Vector3& parentScale = m_parent->GetDerivedScale();
			if (m_inheritScale)
			{
				m_derivedScale = parentScale * m_scale;
			}
			else
			{
				m_derivedScale = m_scale;
			}

			// Our absolute position depends on the parent's orientation and scale
			m_derivedPosition = parentOrientation * (parentScale * m_position);
			m_derivedPosition += m_parent->GetDerivedPosition();
		}
		else
		{
			m_derivedOrientation = m_orientation;
			m_derivedPosition = m_position;
			m_derivedScale = m_scale;
		}

		// Matrix is now invalid, but derived values are valid
		m_cachedTransformInvalid = true;
		m_needParentUpdate = false;
	}
}
