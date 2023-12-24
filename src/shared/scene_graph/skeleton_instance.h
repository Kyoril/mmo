#pragma once

#include "skeleton.h"

namespace mmo
{
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

		void InitAnimationState(AnimationStateSet* animSet);

		void RefreshAnimationState(AnimationStateSet* animSet);

		const String& GetName() const override;

	protected:
		SkeletonPtr m_skeleton;

	protected:
		void CloneBoneAndChildren(const Bone* source, Bone* parent);

		void LoadImpl() override;

		void UnloadImpl() override;
	};
}
