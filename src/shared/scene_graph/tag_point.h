#pragma once

#include "bone.h"
#include "entity.h"

namespace mmo
{
    class Entity;
    class MovableObject;

	class TagPoint final : public Bone
	{
    public:
        TagPoint(uint16 handle, Skeleton& creator);
        ~TagPoint() override = default;

        Entity* GetParentEntity() const { return mParentEntity; }
        MovableObject* GetChildObject() const { return mChildObject; }

        void SetParentEntity(Entity* pEntity) { mParentEntity = pEntity; }
        void SetChildObject(MovableObject* pObject) { mChildObject = pObject; }

        void SetInheritParentEntityOrientation(bool inherit) { mInheritParentEntityOrientation = inherit; NeedUpdate(); }

        bool GetInheritParentEntityOrientation() const { return mInheritParentEntityOrientation; }

        void SetInheritParentEntityScale(bool inherit) { mInheritParentEntityScale = inherit; NeedUpdate(); }

        bool GetInheritParentEntityScale() const { return mInheritParentEntityScale; }

        const Matrix4& GetParentEntityTransform() const { return mParentEntity->GetParentNodeFullTransform(); }

        const Matrix4& GetFullLocalTransform() const { return mFullLocalTransform; }

        void NeedUpdate(bool forceParentUpdate = false) override;

        void UpdateFromParentImpl() const override;

    private:
        Entity* mParentEntity;
        MovableObject* mChildObject;
        mutable Matrix4 mFullLocalTransform;
        bool mInheritParentEntityOrientation;
        bool mInheritParentEntityScale;
	};
}
