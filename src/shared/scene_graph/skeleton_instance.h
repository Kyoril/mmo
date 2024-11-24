#pragma once

#include "skeleton.h"

namespace mmo
{
	class TagPoint;
	class AnimationStateSet;

	class SkeletonInstance : public Skeleton
	{
	public:
		SkeletonInstance(SkeletonPtr masterCopy);
		virtual ~SkeletonInstance() override;

	public:
		uint16 GetNumAnimations() const override;

		Animation* GetAnimation(uint16 index) const override;

		Animation* GetAnimationImpl(const String& name, const LinkedSkeletonAnimationSource** linker = nullptr) const override;

		Animation& CreateAnimation(const String& name, float duration) override;

		Animation* GetAnimation(const String& name, const LinkedSkeletonAnimationSource** linker = nullptr) const override;

		void RemoveAnimation(const String& name) override;

		void InitAnimationState(AnimationStateSet& animSet) override;

		const String& GetName() const override;

		TagPoint* CreateTagPointOnBone(Bone& bone, const Quaternion& offsetOrientation = Quaternion::Identity, const Vector3& offsetPosition = Vector3::Zero);

		void FreeTagPoint(TagPoint& tagPoint);

	protected:
		SkeletonPtr m_skeleton;

	protected:
		void CloneBoneAndChildren(const Bone* source, Bone* parent);

		void LoadImpl() override;

		void UnloadImpl() override;

	private:
		std::vector<std::unique_ptr<TagPoint>> m_tagPoints;

		uint16 m_nextTagPointHandle{ 0 };
	};
}
