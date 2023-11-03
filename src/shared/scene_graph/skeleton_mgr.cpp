
#include "skeleton_mgr.h"

namespace mmo
{
	SkeletonMgr& SkeletonMgr::Get()
	{
		static SkeletonMgr s_instance;
		return s_instance;
	}

	SkeletonPtr SkeletonMgr::Load(const String& name)
	{
		if (m_skeletonsByName.contains(name))
		{
			return m_skeletonsByName[name];
		}

		const SkeletonPtr skeleton = std::make_shared<Skeleton>(name);
		return m_skeletonsByName[name] = skeleton;
	}

	void SkeletonMgr::Unload(const String& name)
	{
		if (m_skeletonsByName.contains(name))
		{
			m_skeletonsByName.erase(name);
		}
	}
}
