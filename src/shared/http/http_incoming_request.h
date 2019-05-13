// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "network/receive_state.h"
#include "binary_io/reader.h"
#include "binary_io/memory_source.h"

#include <map>
#include <functional>

namespace mmo
{
	namespace net
	{
		namespace http
		{
			struct IncomingRequest : io::Reader
			{
				enum Type
				{
					Head,
					Get,
					Post,
					Options,

					Unknown_
				};

				typedef std::map<String, String> Headers;
				typedef std::map<String, String> Arguments;

				IncomingRequest();
				Type getType() const;
				const String &getPath() const;
				const String &getHost() const;
				const String &getPostData() const;
				const Headers &getHeaders() const;
				const Arguments &getPathArguments() const { return m_pathArguments; }

				static ReceiveState start(IncomingRequest &packet, io::MemorySource &source);

			private:

				Type m_type;
				String m_path;
				String m_host;
				String m_postData;
				Headers m_headers;
				Arguments m_pathArguments;
			};


			bool authorize(
			    const IncomingRequest &request,
			    const std::function<bool (const std::string &, const std::string &)> &checkCredentials);
		}
	}
}
