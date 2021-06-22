// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "scene_node.h"
#include "scene.h"


namespace mmo
{
	SceneNode::SceneNode(String name)
		: m_name(std::move(name))
		, m_parent(nullptr)
		, m_needParentUpdate(false)
		, m_orientation(Quaternion::Identity)
		, m_derivedOrientation(Quaternion::Identity)
		, m_inheritOrientation(true)
		, m_scale(Vector3::UnitScale)
		, m_derivedScale(Vector3::UnitScale)
		, m_inheritScale(true)
		, m_cachedTransformInvalid(true)
	{
	}

	void SceneNode::SetParent(SceneNode* parent)
	{
		if (m_parent == parent)
		{
			return;
		}
		
		m_parent = parent;
		Invalidate();

		if (m_parent)
		{
			nodeAttached(*this);
		}
		else
		{
			nodeDetached(*this);
		}
	}

	void SceneNode::Invalidate()
	{
		m_needParentUpdate = true;
		m_cachedTransformInvalid = true;
	}

	const Quaternion& SceneNode::GetDerivedOrientation()
	{
		if (m_needParentUpdate)
		{
			UpdateFromParent();
		}
		
		return m_derivedOrientation;
	}

	void SceneNode::SetOrientation(const Quaternion& orientation)
	{
		m_orientation = orientation;
		m_orientation.Normalize();

		Invalidate();
	}

	void SceneNode::SetDerivedOrientation(const Quaternion& orientation)
	{
        if(m_parent)
        {
			SetOrientation( m_parent->ConvertWorldToLocalOrientation(orientation));   
        }
	}

	void SceneNode::Roll(const Radian& angle, TransformSpace relativeTo)
	{
		Rotate(Vector3::UnitZ, angle, relativeTo);
	}

	void SceneNode::Pitch(const Radian& angle, TransformSpace relativeTo)
	{
		Rotate(Vector3::UnitX, angle, relativeTo);
	}

	void SceneNode::Yaw(const Radian& angle, TransformSpace relativeTo)
	{
		Rotate(Vector3::UnitY, angle, relativeTo);
	}

	void SceneNode::Rotate(const Vector3& axis, const Radian& angle, TransformSpace relativeTo)
	{
		Quaternion q;
		q.FromAngleAxis(angle, axis);
		Rotate(q, relativeTo);
	}

	void SceneNode::Rotate(const Quaternion& delta, TransformSpace relativeTo)
	{
		Quaternion norm = delta;
		norm.Normalize();

		switch(relativeTo)
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

		Invalidate();
	}

	const Vector3& SceneNode::GetDerivedScale()
	{
		if (m_needParentUpdate)
		{
			UpdateFromParent();
		}
		
		return m_derivedScale;
	}

	void SceneNode::SetScale(const Vector3& scale)
	{
		m_scale = scale;
        Invalidate();
	}

	void SceneNode::Scale(const Vector3& scaleOnAxis)
	{
        m_scale = scaleOnAxis * m_scale;
        Invalidate();
	}

	const Vector3& SceneNode::GetDerivedPosition()
	{
		if (m_needParentUpdate)
		{
			UpdateFromParent();
		}
		
		return m_derivedPosition;
	}

	void SceneNode::SetPosition(const Vector3& position)
	{
		m_position = position;
        Invalidate();
	}

	void SceneNode::SetDerivedPosition(const Vector3& position)
	{
		if (m_parent)
		{
			SetPosition(m_parent->ConvertWorldToLocalPosition(position));
		}
	}

	Vector3 SceneNode::ConvertWorldToLocalPosition(const Vector3& worldPos)
	{
		if (m_needParentUpdate)
        {
            UpdateFromParent();
        }
		
		return m_derivedOrientation.Inverse() * (worldPos - m_derivedPosition) / m_derivedScale;
	}

	Vector3 SceneNode::ConvertLocalToWorldPosition(const Vector3& localPos)
	{
		if (m_needParentUpdate)
        {
            UpdateFromParent();
        }
		
		return m_derivedOrientation * (localPos * m_derivedScale) + m_derivedPosition;
	}

	Quaternion SceneNode::ConvertWorldToLocalOrientation(const Quaternion& worldOrientation)
	{
		if (m_needParentUpdate)
        {
            UpdateFromParent();
        }
		
		return m_derivedOrientation.Inverse() * worldOrientation;
	}

	Quaternion SceneNode::ConvertLocalToWorldOrientation(const Quaternion& localOrientation)
	{
		if (m_needParentUpdate)
        {
            UpdateFromParent();
        }
		
		return m_derivedOrientation * localOrientation;
	}

	void SceneNode::SetInheritOrientation(bool inherit)
	{
        m_inheritOrientation = inherit;
        Invalidate();
	}

	void SceneNode::SetInheritScale(bool inherit)
	{
        m_inheritScale = inherit;
        Invalidate();
	}

	void SceneNode::Translate(const Vector3& delta, TransformSpace relativeTo)
	{
		switch(relativeTo)
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

		Invalidate();
	}

	void SceneNode::AddChild(SceneNode& child)
	{
		ASSERT(!child.m_parent);

		m_children.emplace(ChildNodeMap::value_type(child.GetName(), &child));
		child.SetParent(this);
	}

	SceneNode* SceneNode::RemoveChild(const String& name)
	{
		const auto it = m_children.find(name);
		if (it == m_children.end())
		{
			return nullptr;
		}

		SceneNode* child = it->second;
		m_children.erase(it);

		child->SetParent(nullptr);
		return child;
	}

	void SceneNode::UpdateFromParent()
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
		
		updated(*this);
	}
}
