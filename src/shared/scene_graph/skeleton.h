#pragma once

#include "base/typedefs.h"
#include "animation.h"
#include "bone.h"

#include <memory>

namespace mmo
{
	struct LinkedSkeletonAnimationSource;

	class Skeleton
	{
	public:

		virtual Bone* CreateBone();

		virtual Bone* CreateBone(uint16 handle);

		virtual Bone* CreateBone(const String& name);

		virtual Bone* CreateBone(const String& name, uint16 handle);

		virtual uint16 GetNumBones() const;

		virtual Bone* GetRootBone() const;

		virtual Bone* GetBone(unsigned short handle) const;

		virtual Bone* GetBone(const String& name) const;

		virtual bool HasBone(const String& name) const;

		virtual void SetBindingPose();

		virtual void Reset(bool resetManualBones = false);

		virtual Animation* CreateAnimation(const String& name, const float duration);

		virtual Animation* GetAnimation(const String& name, const LinkedSkeletonAnimationSource** linker) const;

		virtual Animation* GetAnimation(const String& name) const;

		virtual Animation* GetAnimationImpl(const String& name, const LinkedSkeletonAnimationSource** linker = nullptr) const;

		virtual bool HasAnimation(const String& name) const;

		virtual void RemoveAnimation(const String& name);
	};


	typedef std::shared_ptr<Skeleton> SkeletonPtr;

	/// Link to another skeleton to share animations
	struct LinkedSkeletonAnimationSource
	{
		String skeletonName;
		SkeletonPtr skeleton;
		float scale;

		LinkedSkeletonAnimationSource(const String& skelName, float scl)
			: skeletonName(skelName)
			, scale(scl)
		{}

		LinkedSkeletonAnimationSource(const String& skelName, float scl, SkeletonPtr skelPtr)
			: skeletonName(skelName)
			, skeleton(skelPtr)
			, scale(scl) {}
	};
}
