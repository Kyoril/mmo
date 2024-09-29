// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "scene_node.h"
#include "scene.h"
#include "movable_object.h"
#include "render_queue.h"


namespace mmo
{
	void SceneNode::FindVisibleObjects(Camera& camera, RenderQueue& renderQueue, VisibleObjectsBoundsInfo& visibleObjectBounds, bool includeChildren)
	{
		// TODO: Check if this object is visible itself

		for (auto& [name, object] : m_objectsByName)
		{
			renderQueue.ProcessVisibleObject(*object, camera, visibleObjectBounds);
		}

		if (includeChildren)
		{
			for (const auto& [name, child] : m_children)
			{
				static_cast<SceneNode*>(child)->FindVisibleObjects(camera, renderQueue, visibleObjectBounds, includeChildren);
			}
		}
	}

	void SceneNode::AutoTrack()
	{
		// NB assumes that all scene nodes have been updated
		if (!m_autoTrackTarget)
		{
			return;
		}

		LookAt(m_autoTrackTarget->GetDerivedPosition() + m_autoTrackOffset, TransformSpace::World, m_autoTrackLocalDirection);
		Update(true, true);
	}

	SceneNode* SceneNode::GetParentSceneNode() const
	{
		return dynamic_cast<SceneNode*>(GetParent());
	}

	void SceneNode::RemoveFromParent() const
	{
		if (m_parent)
		{
			m_parent->RemoveChild(m_name);
		}
	}

	void SceneNode::SetVisible(const bool visible, const bool cascade)
	{
		for (const auto& [name, object] : m_objectsByName)
		{
			object->SetVisible(visible);
		}

		if (cascade)
		{
			for (auto& [name, node] : m_children)
			{
				auto* sceneNode = dynamic_cast<SceneNode*>(node);
				ASSERT(sceneNode);
				sceneNode->SetVisible(visible, true);
			}
		}
	}

	void SceneNode::ToggleVisibility(bool cascade)
	{
		for (const auto& [name, object] : m_objectsByName)
		{
			object->SetVisible(!object->IsVisible());
		}

		if (cascade)
		{
			for (auto& [name, node] : m_children)
			{
				auto* sceneNode = dynamic_cast<SceneNode*>(node);
				ASSERT(sceneNode);
				sceneNode->ToggleVisibility(cascade);
			}
		}
	}

	SceneNode::SceneNode(Scene& scene)
		: Node()
		, m_scene(scene)
	{
	}

	SceneNode::SceneNode(Scene& scene, const String& name)
		: Node(name)
		, m_scene(scene)
	{
	}

	SceneNode::~SceneNode()
	{
		DetachAllObjects();
	}

	void SceneNode::Update(const bool updateChildren, const bool parentHasChanged)
	{
		Node::Update(updateChildren, parentHasChanged);
		UpdateBounds();
	}

	void SceneNode::UpdateFromParentImpl() const
	{
		Node::UpdateFromParentImpl();

		for (const auto& [name, obj] : m_objectsByName)
		{
			obj->NotifyMoved();
		}
	}

	Node* SceneNode::CreateChildImpl()
	{
		return &m_scene.CreateSceneNode();
	}

	Node* SceneNode::CreateChildImpl(const String& name)
	{
		return &m_scene.CreateSceneNode(name);
	}

	void SceneNode::SetParent(Node* parent)
	{
		Node::SetParent(parent);

		if (parent)
		{
			const auto* sceneParent = dynamic_cast<SceneNode*>(parent);
			SetInSceneGraph(sceneParent->IsInSceneGraph());
		}
		else
		{
			SetInSceneGraph(false);
		}
	}

	void SceneNode::SetInSceneGraph(bool inSceneGraph)
	{
		if (inSceneGraph != m_isInSceneGraph)
		{
			m_isInSceneGraph = inSceneGraph;

			for (const auto& [name, child] : m_children)
			{
				auto* sceneChild = dynamic_cast<SceneNode*>(child);
				sceneChild->SetInSceneGraph(inSceneGraph);
			}
		}
	}

	void SceneNode::UpdateBounds()
	{
		m_worldAABB.SetNull();

		// Iterate through all objects
		for (const auto& [name, child] : m_objectsByName)
		{
			m_worldAABB.Combine(child->GetWorldBoundingBox(true));
		}

		// Merge with children
		for (const auto& childIt : m_children)
		{
			// We expect the child node to already be up to date here, so use it's bound as is
			m_worldAABB.Combine(static_cast<SceneNode*>(childIt.second)->m_worldAABB);
		}
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

	MovableObject* SceneNode::GetAttachedObject(uint32 index) const
	{
		if (index < m_objectsByName.size())
		{
			auto it = m_objectsByName.begin();
			std::advance(it, index);
			return it->second;
		}

		return nullptr;
	}

	MovableObject* SceneNode::GetAttachedObject(const String& name) const
	{
		const auto it = m_objectsByName.find(name);
		if (it != m_objectsByName.end())
		{
			return it->second;
		}

		return nullptr;
	}

	void SceneNode::DetachObject(MovableObject& object)
	{
		std::erase_if(m_objectsByName, [&object](const auto& item)
		{
			auto const& [key, value] = item;
			return value == &object;
		});

        object.NotifyAttachmentChanged(nullptr);

        NeedUpdate();
	}

	MovableObject* SceneNode::DetachObject(const String& name)
	{
		const auto it = m_objectsByName.find(name);
		if (it != m_objectsByName.end())
		{
			MovableObject* obj = it->second;
			m_objectsByName.erase(it);
			obj->NotifyAttachmentChanged(nullptr);
			NeedUpdate();
			return obj;
		}

		return nullptr;
	}

	void SceneNode::DetachAllObjects()
	{
		for (const auto& [name, object] : m_objectsByName)
		{
			object->NotifyAttachmentChanged(nullptr);
		}

		m_objectsByName.clear();
		NeedUpdate();
	}

	SceneNode* SceneNode::CreateChildSceneNode(const Vector3& translate, const Quaternion& rotate)
	{
		return dynamic_cast<SceneNode*>(CreateChild(translate, rotate));
	}

	SceneNode* SceneNode::CreateChildSceneNode(const String& name, const Vector3& translate, const Quaternion& rotate)
	{
		return dynamic_cast<SceneNode*>(CreateChild(name, translate, rotate));
	}

	void SceneNode::SetFixedYawAxis(const bool useFixed, const Vector3& fixedAxis)
	{
		m_yawFixed = useFixed;
		m_yawFixedAxis = fixedAxis;
	}

	void SceneNode::Yaw(const Radian& angle, TransformSpace relativeTo)
	{
		if (m_yawFixed)
		{
			Rotate(m_yawFixedAxis, angle, relativeTo);
		}
		else
		{
			Rotate(Vector3::UnitY, angle, relativeTo);
		}
	}

	void SceneNode::SetDirection(const Vector3& vec, TransformSpace relativeTo, const Vector3& localDirectionVector)
	{
		// Do nothing if given a zero vector
		if (vec == Vector3::Zero) return;

		// The direction we want the local direction point to
		Vector3 targetDir = vec.NormalizedCopy();

		// Transform target direction to world space
		switch (relativeTo)
		{
		case TransformSpace::Parent:
			if (m_inheritOrientation)
			{
				if (m_parent)
				{
					targetDir = m_parent->GetDerivedOrientation() * targetDir;
				}
			}
			break;
		case TransformSpace::Local:
			targetDir = GetDerivedOrientation() * targetDir;
			break;
		case TransformSpace::World:
			// default orientation
			break;
		}

		// Calculate target orientation relative to world space
		Quaternion targetOrientation;
		if (m_yawFixed)
		{
			// Calculate the quaternion for rotate local Z to target direction
			Vector3 xVec = m_yawFixedAxis.Cross(targetDir);
			xVec.Normalize();
			Vector3 yVec = targetDir.Cross(xVec);
			yVec.Normalize();
			const auto unitZToTarget = Quaternion(xVec, yVec, targetDir);

			if (localDirectionVector == Vector3::NegativeUnitZ)
			{
				// Special case for avoid calculate 180 degree turn
				targetOrientation = Quaternion(-unitZToTarget.y, -unitZToTarget.z, unitZToTarget.w, unitZToTarget.x);
			}
			else
			{
				// Calculate the quaternion for rotate local direction to target direction
				const Quaternion localToUnitZ = localDirectionVector.GetRotationTo(Vector3::UnitZ);
				targetOrientation = unitZToTarget * localToUnitZ;
			}
		}
		else
		{
			const Quaternion& currentOrient = GetDerivedOrientation();

			// Get current local direction relative to world space
			const Vector3 currentDir = currentOrient * localDirectionVector;
			if ((currentDir + targetDir).GetSquaredLength() < 0.00005f)
			{
				// Oops, a 180 degree turn (infinite possible rotation axes)
				// Default to yaw i.e. use current UP
				targetOrientation =
					Quaternion(-currentOrient.y, -currentOrient.z, currentOrient.w, currentOrient.x);
			}
			else
			{
				// Derive shortest arc to new direction
				const Quaternion rotQuat = currentDir.GetRotationTo(targetDir);
				targetOrientation = rotQuat * currentOrient;
			}
		}

		// Set target orientation, transformed to parent space
		if (m_parent && m_inheritOrientation)
		{
			SetOrientation(m_parent->GetDerivedOrientation().UnitInverse() * targetOrientation);
		}
		else
		{
			SetOrientation(targetOrientation);
		}
	}

	void SceneNode::LookAt(const Vector3& targetPoint, TransformSpace relativeTo, const Vector3& localDirectionVector)
	{
		Vector3 origin;
		switch (relativeTo)
		{
		case TransformSpace::World:
			origin = GetDerivedPosition();
			break;
		case TransformSpace::Parent:
			origin = m_position;
			break;
		case TransformSpace::Local:
			origin = Vector3::Zero;
			break;
		}

		SetDirection(targetPoint - origin, relativeTo, localDirectionVector);
	}

	void SceneNode::SetAutoTracking(bool enabled, SceneNode* const target, const Vector3& localDirectionVector,
		const Vector3& offset)
	{
		if (enabled)
		{
			m_autoTrackTarget = target;
			m_autoTrackOffset = offset;
			m_autoTrackLocalDirection = localDirectionVector;
		}
		else
		{
			m_autoTrackTarget = nullptr;
		}

		// TODO: Subscribe to scene's auto tracking list so this node gets automatically updated it's auto tracking target
	}
}
