
#include "skeleton_instance.h"

namespace mmo
{
	SkeletonInstance::SkeletonInstance(SkeletonPtr masterCopy)
		: Skeleton()
		, m_skeleton(std::move(masterCopy))
	{
	}

	SkeletonInstance::~SkeletonInstance()
	{
		SkeletonInstance::UnloadImpl();
	}

	uint16 SkeletonInstance::GetNumAnimations() const
	{
		return m_skeleton->GetNumAnimations();
	}

	Animation* SkeletonInstance::GetAnimation(const uint16 index) const
	{
		return m_skeleton->GetAnimation(index);
	}

	Animation* SkeletonInstance::GetAnimationImpl(const String& name,
		const LinkedSkeletonAnimationSource** linker) const
	{
		return m_skeleton->GetAnimationImpl(name, linker);
	}

	Animation& SkeletonInstance::CreateAnimation(const String& name, const float duration)
	{
		return m_skeleton->CreateAnimation(name, duration);
	}

	Animation* SkeletonInstance::GetAnimation(const String& name, const LinkedSkeletonAnimationSource** linker) const
	{
		return m_skeleton->GetAnimation(name, linker);
	}

	void SkeletonInstance::RemoveAnimation(const String& name)
	{
		return m_skeleton->RemoveAnimation(name);
	}

	void SkeletonInstance::InitAnimationState(AnimationStateSet& animSet)
	{
		m_skeleton->InitAnimationState(animSet);
	}

	const String& SkeletonInstance::GetName() const
	{
		return m_skeleton->GetName();
	}

	void SkeletonInstance::CloneBoneAndChildren(const Bone* source, Bone* parent)
	{
		Bone* newBone;
		if (source->GetName().empty())
		{
			newBone = CreateBone(source->GetHandle());
		}
		else
		{
			newBone = CreateBone(source->GetName(), source->GetHandle());
		}

		if (parent == nullptr)
		{
			m_rootBones.push_back(newBone);
		}
		else
		{
			parent->AddChild(*newBone);
		}

		newBone->SetOrientation(source->GetOrientation());
		newBone->SetPosition(source->GetPosition());
		newBone->SetScale(source->GetScale());

		// Process children
		for (uint32 i = 0; i < source->GetNumChildren(); ++i)
		{
			CloneBoneAndChildren(dynamic_cast<Bone*>(source->GetChild(i)), newBone);
		}
	}

	void SkeletonInstance::LoadImpl()
	{
		m_nextAutoHandle = m_skeleton->m_nextAutoHandle;

		m_blendState = m_skeleton->m_blendState;

		if (m_skeleton->m_rootBones.empty())
		{
			m_skeleton->DeriveRootBone();
		}

		for (const auto& bone : m_skeleton->m_rootBones)
		{
			CloneBoneAndChildren(bone, nullptr);
			bone->Update(true, false);
		}

		SetBindingPose();
	}

	void SkeletonInstance::UnloadImpl()
	{
		Skeleton::UnloadImpl();
	}
}
