
#include "skeleton.h"

namespace mmo
{
    static constexpr uint16 MaxBoneCount = 256;

	Skeleton::Skeleton(const String& name)
		: m_name(name)
	{
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
        ASSERT(m_boneListByName.find(ret->GetName()) == m_boneListByName.end());

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
        ASSERT(handle >= m_boneList.size() && m_boneList[handle] == nullptr);
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
        return m_boneList.size();
	}

	Bone* Skeleton::GetRootBone() const
	{
        if (m_rootBones.empty())
        {
            DeriveRootBone();
        }

        return m_rootBones[0];
	}

	Bone* Skeleton::GetBone(unsigned short handle) const
	{
        ASSERT(handle < m_boneList.size());
        return m_boneList[handle].get();
	}

	Bone* Skeleton::GetBone(const String& name) const
	{
		const auto i = m_boneListByName.find(name);
        ASSERT(i != m_boneListByName.end());

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

	void Skeleton::Reset(bool resetManualBones)
	{
		for (const auto& bone : m_boneList)
		{
			if (!bone->IsManuallyControlled() || resetManualBones)
			{
				bone->Reset();
			}
		}
	}

	Animation* Skeleton::CreateAnimation(const String& name, const float duration)
	{
		// TODO
		return nullptr;
	}

	Animation* Skeleton::GetAnimation(const String& name, const LinkedSkeletonAnimationSource** linker) const
	{
		// TODO
		return nullptr;
	}

	Animation* Skeleton::GetAnimation(const String& name) const
	{
		// TODO
		return nullptr;
	}

	Animation* Skeleton::GetAnimationImpl(const String& name, const LinkedSkeletonAnimationSource** linker) const
	{
		// TODO
		return nullptr;
	}

	bool Skeleton::HasAnimation(const String& name) const
	{
		// TODO
		return false;
	}

	void Skeleton::RemoveAnimation(const String& name)
	{
		// TODO
	}

	void Skeleton::GetBoneMatrices(Matrix4* matrices)
	{
		UpdateTransforms();

		for (const auto& bone : m_boneList)
		{
			bone->GetOffsetTransform(*matrices);
			matrices++;
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

	void Skeleton::AddLinkedSkeletonAnimationSource(const String& skelName, float scale)
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
}
