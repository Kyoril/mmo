// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "scene_node.h"
#include "scene.h"
#include "movable_object.h"
#include "base/unordered_map_erase_if.h"


namespace mmo
{
	void SceneNode::RemoveAllChildren()
	{
		m_children.clear();
	}

	SceneNode::SceneNode(Scene& scene)
		: m_parent(nullptr)
		, m_needParentUpdate(false)
		, m_parentNotified(false)
		, m_orientation(Quaternion::Identity)
		, m_derivedOrientation(Quaternion::Identity)
		, m_inheritOrientation(true)
		, m_scale(Vector3::UnitScale)
		, m_derivedScale(Vector3::UnitScale)
		, m_inheritScale(true)
		, m_cachedTransformInvalid(true)
	{
		static uint32 index = 0;
		m_name = "SceneNode_" + std::to_string(index);
	}

	SceneNode::SceneNode(Scene& scene, String name)
		: m_name(std::move(name))
		, m_parent(nullptr)
		, m_needParentUpdate(false)
		, m_parentNotified(false)
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
		m_parentNotified = false;
		NeedUpdate();

		if (m_parent)
		{
			nodeAttached(*this);
		}
		else
		{
			nodeDetached(*this);
		}
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

		NeedUpdate();
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

		NeedUpdate();
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
        NeedUpdate();
	}

	void SceneNode::Scale(const Vector3& scaleOnAxis)
	{
        m_scale = scaleOnAxis * m_scale;
        NeedUpdate();
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
        NeedUpdate();
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
        NeedUpdate();
	}

	void SceneNode::SetInheritScale(bool inherit)
	{
        m_inheritScale = inherit;
        NeedUpdate();
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

		NeedUpdate();
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

	void SceneNode::Update(bool updateChildren, bool parentHasChanged)
	{
		if (m_needParentUpdate || parentHasChanged)
		{
			UpdateFromParent();
		}

		if (updateChildren)
		{
			if (m_needChildUpdates || parentHasChanged)
			{
				for(auto& child : m_children)
				{
					child.second->Update(true, true);
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

		UpdateBounds();
	}

	void SceneNode::UpdateBounds()
	{
		m_bounds.SetNull();

		// Iterate through all objects
		for (auto& it : m_objectsByName)
		{
			m_bounds.Combine(it.second->GetWorldBoundingBox(true));
		}
	}

	const Matrix4& SceneNode::GetFullTransform()
	{
		if (m_cachedTransformInvalid)
		{
			m_cachedTransform.MakeTransform(
				GetDerivedPosition(),
				GetDerivedScale(),
				GetDerivedOrientation());
			m_cachedTransformInvalid = false;
		}

		return m_cachedTransform;
	}

	void SceneNode::AttachObject(MovableObject& obj)
	{
		ASSERT(!obj.IsAttached());
		
        obj.NotifyAttachmentChanged(this);
		
        // Also add to name index
        const std::pair<ObjectMap::iterator, bool> insresult = 
            m_objectsByName.insert(ObjectMap::value_type(obj.GetName(), &obj));
        assert(insresult.second && "Object was not attached because an object of the same name was already attached to this scene node.");
        (void)insresult;
        
        NeedUpdate();
	}

	void SceneNode::DetachObject(MovableObject& object)
	{
		std::erase_if(m_objectsByName, [&object](const std::pair<String, MovableObject*> value)
		{
			return value.second == &object;
		});

        object.NotifyAttachmentChanged(nullptr);

        NeedUpdate();
	}

	void SceneNode::NeedUpdate(bool forceParentUpdate)
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

	void SceneNode::RequestUpdate(SceneNode& child, bool forceParentUpdate)
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
