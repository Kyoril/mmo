#pragma once

#include "base/typedefs.h"


namespace mmo
{
	namespace updating
	{
		enum UpdateURLScheme
		{
		    UP_FileSystem,
		    UP_HTTP
		};


		struct UpdateURL
		{
			UpdateURLScheme scheme;
			std::string host;
			uint16 port;
			std::string path;


			explicit UpdateURL(const std::string &url);
			UpdateURL(
			    UpdateURLScheme scheme,
			    std::string host,
				uint16 port,
			    std::string path);
		};


		bool operator == (const UpdateURL &left, const UpdateURL &right);
	}
}
