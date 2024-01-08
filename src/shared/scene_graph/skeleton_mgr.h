#pragma once

#include "base/non_copyable.h"
#include "skeleton.h"

namespace mmo
{
	class SkeletonMgr final
		: public NonCopyable
	{
	private:
		SkeletonMgr() = default;

	public:
		static SkeletonMgr& Get();

	public:
		SkeletonPtr Load(const String& name);

		void Unload(const String& name);

	private:
		std::map<String, SkeletonPtr> m_skeletonsByName;
	};
}