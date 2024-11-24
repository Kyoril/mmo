
#include "tag_point.h"
#include "entity.h"
#include "scene_node.h"
#include "skeleton_instance.h"
#include "math/matrix4.h"

namespace mmo
{
	TagPoint::TagPoint(uint16 handle, Skeleton& creator)
		: Bone(handle, creator)
		, mParentEntity(0)
		, mChildObject(0)
		, mInheritParentEntityOrientation(true)
		, mInheritParentEntityScale(true)
	{
	}

	void TagPoint::NeedUpdate(bool forceParentUpdate)
	{
		Bone::NeedUpdate(forceParentUpdate);

		// We need to tell parent entities node
		if (mParentEntity)
		{
			Node* n = mParentEntity->GetParentSceneNode();
			if (n)
			{
				n->NeedUpdate();
			}

		}
	}

	void TagPoint::UpdateFromParentImpl() const
	{
		Bone::UpdateFromParentImpl();

        // Save transform for local skeleton
        mFullLocalTransform.MakeTransform(
            m_derivedPosition,
            m_derivedScale,
            m_derivedOrientation);

        // Include Entity transform
        if (mParentEntity)
        {
            Node* entityParentNode = mParentEntity->GetParentSceneNode();
            if (entityParentNode)
            {
                // Note: orientation/scale inherits from parent node already take care with
                // Bone::_updateFromParent, don't do that with parent entity transform.

                // Combine orientation with that of parent entity
                const Quaternion& parentOrientation = entityParentNode->GetDerivedOrientation();
                if (mInheritParentEntityOrientation)
                {
                    m_derivedOrientation = parentOrientation * m_derivedOrientation;
                }

                // Incorporate parent entity scale
                const Vector3& parentScale = entityParentNode->GetDerivedScale();
                if (mInheritParentEntityScale)
                {
                    m_derivedScale *= parentScale;
                }

                // Change position vector based on parent entity's orientation & scale
                m_derivedPosition = parentOrientation * (parentScale * m_derivedPosition);

                // Add altered position vector to parent entity
                m_derivedPosition += entityParentNode->GetDerivedPosition();
            }
        }

        if (mChildObject)
        {
            mChildObject->NotifyMoved();
        }
	}
}
