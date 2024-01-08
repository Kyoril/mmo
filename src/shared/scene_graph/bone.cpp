#include "bone.h"

#include "skeleton.h"

namespace mmo
{
    Bone::Bone(unsigned short handle, Skeleton& creator)
        : Node()
		, m_handle(handle)
		, m_manuallyControlled(false)
		, m_creator(creator)
    {
    }

    Bone::Bone(const String& name, unsigned short handle, Skeleton& creator)
        : Node(name)
		, m_handle(handle)
		, m_manuallyControlled(false)
		, m_creator(creator)
    {
    }

    Bone* Bone::CreateChild(unsigned short handle, const Vector3& inTranslate, const Quaternion& inRotate)
    {
        Bone* retBone = m_creator.CreateBone(handle);
        retBone->Translate(inTranslate);
        retBone->Rotate(inRotate);
        AddChild(*retBone);
        return retBone;
    }

    Node* Bone::CreateChildImpl(void)
    {
        return m_creator.CreateBone();
    }

    Node* Bone::CreateChildImpl(const String& name)
    {
        return m_creator.CreateBone(name);
    }

    void Bone::SetBindingPose()
    {
        SetInitialState();

        // Save inverse derived position/scale/orientation, used for calculate offset transform later
        m_bindDerivedInversePosition = - GetDerivedPosition();
        m_bindDerivedInverseScale = Vector3::UnitScale / GetDerivedScale();
        m_bindDerivedInverseOrientation = GetDerivedOrientation().Inverse();
    }

    void Bone::Reset(void)
    {
        ResetToInitialState();
    }

    void Bone::SetManuallyControlled(bool manuallyControlled)
    {
        m_manuallyControlled = manuallyControlled;
        m_creator.NotifyManualBoneStateChange(*this);
    }

    bool Bone::IsManuallyControlled() const
	{
        return m_manuallyControlled;
    }

    void Bone::GetOffsetTransform(Matrix4& m) const
    {
        // Combine scale with binding pose inverse scale,
        // NB just combine as equivalent axes, no shearing
        Vector3 locScale = GetDerivedScale() * m_bindDerivedInverseScale;

        // Combine orientation with binding pose inverse orientation
        const Quaternion locRotate = GetDerivedOrientation() * m_bindDerivedInverseOrientation;

        // Combine position with binding pose inverse position,
        // Note that translation is relative to scale & rotation,
        // so first reverse transform original derived position to
        // binding pose bone space, and then transform to current
        // derived bone space.
        const Vector3 locTranslate = GetDerivedPosition() + locRotate * (locScale * m_bindDerivedInversePosition);
        m.MakeTransform(locTranslate, locScale, locRotate);
    }

    unsigned short Bone::GetHandle(void) const
    {
        return m_handle;
    }

    void Bone::NeedUpdate(bool forceParentUpdate)
    {
        Node::NeedUpdate(forceParentUpdate);

        if (IsManuallyControlled())
        {
            // Dirty the skeleton if manually controlled so animation can be updated
            m_creator.NotifyManualBonesDirty();
        }
    }
}
