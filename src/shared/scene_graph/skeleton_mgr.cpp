
#include "skeleton_mgr.h"

#include "skeleton_serializer.h"
#include "assets/asset_registry.h"
#include "binary_io/stream_source.h"
#include "log/default_log_levels.h"

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

		// Try to load the file from the registry
		const auto filePtr = AssetRegistry::OpenFile(name);
		if (!filePtr)
		{
			ELOG("Unable to load mesh file " << name);
			return nullptr;
		}

		// Create readers
		io::StreamSource source{ *filePtr };
		io::Reader reader{ source };

		// Create the resulting skeleton
		const auto skeleton = std::make_shared<Skeleton>(name);

		SkeletonDeserializer deserializer{ *skeleton };
		if (!deserializer.Read(reader))
		{
			ELOG("Failed to load skeleton!");
			return nullptr;
		}

		skeleton->Load();

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
