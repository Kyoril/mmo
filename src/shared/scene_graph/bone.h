#pragma once

#include "base/typedefs.h"
#include "node.h"
#include "math/matrix4.h"
#include "math/vector3.h"

namespace mmo
{
	class Skeleton;

	class Bone : public Node
	{
	public:
		Bone(unsigned short handle, Skeleton& creator);
		Bone(const String& name, unsigned short handle, Skeleton& creator);
		~Bone() override = default;

	public:
        Bone* CreateChild(unsigned short handle, const Vector3& translate = Vector3::Zero, const Quaternion& rotate = Quaternion::Identity);

        unsigned short GetHandle() const;

        void SetBindingPose();

        void Reset();

		void SetManuallyControlled(bool manuallyControlled);

		bool IsManuallyControlled() const;

		void GetOffsetTransform(Matrix4& m) const;

		const Vector3& GetBindingPoseInverseScale() const { return m_bindDerivedInverseScale; }

		const Vector3& GetBindingPoseInversePosition() const { return m_bindDerivedInversePosition; }

		const Quaternion& GetBindingPoseInverseOrientation() const { return m_bindDerivedInverseOrientation; }

		void NeedUpdate(bool forceParentUpdate = false) override;

	protected:
		Node* CreateChildImpl();

		Node* CreateChildImpl(const String& name);


    protected:
        unsigned short m_handle;

        bool m_manuallyControlled;

        Skeleton& m_creator;

        Vector3 m_bindDerivedInverseScale;

        Quaternion m_bindDerivedInverseOrientation;

        Vector3 m_bindDerivedInversePosition;
	};
}
