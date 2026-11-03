// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "resource_data.h"
#include "resource_names.h"

#include "log/default_log_levels.h"

#include <fstream>
#include <iterator>
#include <mutex>
#include <unordered_map>
#include <vector>

#import <Foundation/Foundation.h>

namespace mmo
{
	ResourceBlob LoadResourceBlob(const uint32 id)
	{
		// There is no RCDATA on macOS: the same files are copied into the bundle's
		// Resources directory and read once. The cache is never cleared, which is what
		// gives the blob the process lifetime that ResourceBlob promises.
		static std::mutex s_mutex;
		static std::unordered_map<uint32, std::vector<uint8>> s_cache;

		const std::scoped_lock lock{ s_mutex };
		if (const auto it = s_cache.find(id); it != s_cache.end())
		{
			return ResourceBlob{ it->second.data(), it->second.size() };
		}

		const char* fileName = nullptr;
		for (const ResourceName& entry : ResourceNames)
		{
			if (entry.id == id)
			{
				fileName = entry.fileName;
				break;
			}
		}

		if (!fileName)
		{
			ELOG("Embedded resource " << id << " has no file name");
			return {};
		}

		std::string path;
		@autoreleasepool
		{
			NSString* resources = [[NSBundle mainBundle] resourcePath];
			if (!resources)
			{
				ELOG("The launcher bundle has no resource directory");
				return {};
			}
			path = std::string([resources fileSystemRepresentation]) + "/" + fileName;
		}

		std::ifstream file(path, std::ios::binary);
		if (!file)
		{
			ELOG("Embedded resource " << id << " not found at " << path);
			return {};
		}

		std::vector<uint8> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		if (data.empty())
		{
			ELOG("Embedded resource " << id << " is empty");
			return {};
		}

		// Moving the vector into the map keeps its heap buffer, so the pointer is stable.
		const auto& stored = s_cache.emplace(id, std::move(data)).first->second;
		return ResourceBlob{ stored.data(), stored.size() };
	}
}
