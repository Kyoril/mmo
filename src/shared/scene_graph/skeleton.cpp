
#include "skeleton.h"

#include "animation_state.h"

namespace mmo
{
    static constexpr uint16 MaxBoneCount = 256;

	Skeleton::Skeleton(String name)
		: AnimationContainer()
		, m_name(std::move(name))
	{
	}

	Skeleton::~Skeleton()
	{
		Unload();
	}

	void Skeleton::Load()
	{
		LoadImpl();
	}

	void Skeleton::Unload()
	{
		UnloadImpl();
	}

	Bone* Skeleton::CreateBone()
	{
		return CreateBone(m_nextAutoHandle++);
	}

	Bone* Skeleton::CreateBone(uint16 handle)
	{
        ASSERT(handle < MaxBoneCount);

        // Check handle not used
        ASSERT(handle >= m_boneList.size() && m_boneList[handle] == nullptr);

        auto ret = std::make_unique<Bone>(handle, *this);
        ASSERT(m_boneListByName.contains(ret->GetName()));

        if (m_boneList.size() <= handle)
        {
            m_boneList.resize(handle + 1);
        }

        m_boneListByName[ret->GetName()] = ret.get();
        return (m_boneList[handle] = std::move(ret)).get();
	}

	Bone* Skeleton::CreateBone(const String& name)
	{
        return CreateBone(name, m_nextAutoHandle++);
	}

	Bone* Skeleton::CreateBone(const String& name, uint16 handle)
	{
        ASSERT(handle < MaxBoneCount);
        ASSERT(handle >= m_boneList.size() || m_boneList[handle] == nullptr);
        ASSERT(!m_boneListByName.contains(name));

        auto ret = std::make_unique<Bone>(name, handle, *this);
        if (m_boneList.size() <= handle)
        {
            m_boneList.resize(handle + 1);
        }

        m_boneListByName[name] = ret.get();
        return (m_boneList[handle] = std::move(ret)).get();
	}

	uint16 Skeleton::GetNumBones() const
	{
		return static_cast<uint16>(m_boneList.size());
	}

	Bone* Skeleton::GetRootBone() const
	{
        if (m_rootBones.empty())
        {
            DeriveRootBone();
        }

        return m_rootBones[0];
	}

	Bone* Skeleton::GetBone(const uint16 handle) const
	{
        ASSERT(handle < m_boneList.size());
        return m_boneList[handle].get();
	}

	Bone* Skeleton::GetBone(const String& name) const
	{
		const auto i = m_boneListByName.find(name);
		if (i == m_boneListByName.end())
		{
			return nullptr;
		}

        return i->second;

	}

	bool Skeleton::HasBone(const String& name) const
	{
		return m_boneListByName.contains(name);
	}

	void Skeleton::SetBindingPose()
	{
		// Update the derived transforms
        UpdateTransforms();

        for (const auto& bone : m_boneList)
        {
	        bone->SetBindingPose();
        }
	}

	void Skeleton::Reset(const bool resetManualBones)
	{
		for (const auto& bone : m_boneList)
		{
			if (!bone->IsManuallyControlled() || resetManualBones)
			{
				bone->Reset();
			}
		}
	}

	Animation& Skeleton::CreateAnimation(const String& name, const float duration)
	{
		ASSERT(!m_animationsList.contains(name));

		auto anim = std::make_unique<Animation>(name, duration);
		anim->NotifyContainer(this);

		return *(m_animationsList[name] = std::move(anim));
	}

	Animation* Skeleton::GetAnimation(const uint16 index) const
	{
		ASSERT(index < m_animationsList.size());

		auto it = m_animationsList.begin();
		std::advance(it, index);

		return it->second.get();
	}

	Animation* Skeleton::GetAnimation(const String& name, const LinkedSkeletonAnimationSource** linker) const
	{
		Animation* anim = GetAnimationImpl(name, linker);
		ASSERT(anim);

		return anim;
	}

	Animation* Skeleton::GetAnimation(const String& name) const
	{
		return GetAnimation(name, nullptr);
	}

	Animation* Skeleton::GetAnimationImpl(const String& name, const LinkedSkeletonAnimationSource** linker) const
	{
		Animation* ret = nullptr;

		if (const auto i = m_animationsList.find(name); i == m_animationsList.end())
		{
			for (auto it = m_linkedSkeletonAnimSourceList.cbegin(); it != m_linkedSkeletonAnimSourceList.cend() && !ret; ++it)
			{
				if (!it->skeleton)
				{
					ret = it->skeleton->GetAnimationImpl(name);
					if (ret && linker)
					{
						*linker = &(*it);
					}
				}
			}
		}
		else
		{
			if (linker)
			{
				*linker = nullptr;
			}

			ret = i->second.get();
		}

		return ret;
	}

	uint16 Skeleton::GetNumAnimations() const
	{
		return static_cast<uint16>(m_animationsList.size());
	}

	void Skeleton::SetAnimationState(const AnimationStateSet& animSet)
	{

	}

	bool Skeleton::HasAnimation(const String& name) const
	{
		return GetAnimationImpl(name) != nullptr;
	}

	void Skeleton::RemoveAnimation(const String& name)
	{
		const auto i = m_animationsList.find(name);
		ASSERT(i != m_animationsList.end());

		m_animationsList.erase(i);
	}

	void Skeleton::GetBoneMatrices(Matrix4* matrices)
	{
		UpdateTransforms();

		for (const auto& bone : m_boneList)
		{
			bone->GetOffsetTransform(*matrices++);
		}
	}

	void Skeleton::UpdateTransforms()
	{
		for (const auto& bone : m_boneList)
		{
			bone->Update(true, false);
		}

		m_manualBonesDirty = false;
	}

	void Skeleton::OptimizeAllAnimations(bool preservingIdentityNodeTracks)
	{
		// TODO
	}

	void Skeleton::AddLinkedSkeletonAnimationSource(const String& skeletonName, float scale)
	{
		// TODO
	}

	void Skeleton::RemoveAllLinkedSkeletonAnimationSources()
	{
		// TODO
	}

	void Skeleton::NotifyManualBonesDirty()
	{
		m_manualBonesDirty = true;
	}

	void Skeleton::NotifyManualBoneStateChange(Bone& bone)
	{
		if (bone.IsManuallyControlled())
		{
			m_manualBones.insert(&bone);
		}
		else
		{
			m_manualBones.erase(&bone);
		}
	}

	void Skeleton::MergeSkeletonAnimations(const Skeleton* source, const BoneHandleMap& boneHandleMap,
		const std::vector<String>& animations)
	{
		// TODO
	}

	void Skeleton::BuildMapBoneByHandle(const Skeleton* source, BoneHandleMap& boneHandleMap) const
	{
		const uint16 numSrcBones = source->GetNumBones();
		boneHandleMap.resize(numSrcBones);

		for (uint16 handle = 0; handle < numSrcBones; ++handle)
		{
			boneHandleMap[handle] = handle;
		}
	}

	void Skeleton::BuildMapBoneByName(const Skeleton* source, BoneHandleMap& boneHandleMap) const
	{
		const uint16 numSrcBones = source->GetNumBones();
		boneHandleMap.resize(numSrcBones);

		uint16 newBoneHandle = this->GetNumBones();
		for (uint16 handle = 0; handle < numSrcBones; ++handle)
		{
			const Bone* srcBone = source->GetBone(handle);
			if (auto i = this->m_boneListByName.find(srcBone->GetName()); i == m_boneListByName.end())
			{
				boneHandleMap[handle] = newBoneHandle++;
			}
			else
			{
				boneHandleMap[handle] = i->second->GetHandle();
			}
		}
	}

	void Skeleton::DeriveRootBone() const
	{
		// Start at the first bone and work up
		ASSERT(!m_boneList.empty());

		m_rootBones.clear();

		for (const auto& bone : m_boneList)
		{
			if (bone->GetParent() == nullptr)
			{
				m_rootBones.push_back(bone.get());
			}
		}
	}

	void Skeleton::LoadImpl()
	{
	}

	void Skeleton::UnloadImpl()
	{
	}
}
