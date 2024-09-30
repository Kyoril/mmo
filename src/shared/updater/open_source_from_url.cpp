// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "open_source_from_url.h"
#include "update_source.h"
#include "update_url.h"
#include "file_system_update_source.h"
#include "https_update_source.h"
#include "http_update_source.h"


namespace mmo
{
	namespace updating
	{
		std::unique_ptr<IUpdateSource> openSourceFromUrl(
		    const UpdateURL &url)
		{
			std::unique_ptr<IUpdateSource> source;

			switch (url.scheme)
			{
			case UP_FileSystem:
				source.reset(new FileSystemUpdateSource(url.path));
				break;

			case UP_HTTP:
				source.reset(new HTTPUpdateSource(
					url.host,
					static_cast<uint16>(url.port ? url.port : 80),
					url.path));
				break;

			case UP_HTTPS:
				source.reset(new HTTPSUpdateSource(
				                 url.host,
				                 static_cast<uint16>(url.port ? url.port : 443),
				                 url.path));
				break;

			default:
				throw std::runtime_error("Unknown URL type");
			}

			return source;
		}
	}
}
