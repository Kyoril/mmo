// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "binary_io/reader.h"
#include "binary_io/memory_source.h"
#include "binary_io/sink.h"

namespace mmo
{
	namespace net
	{
		namespace http
		{
			struct OutgoingAnswer
			{
				enum Status
				{
					OK,

					BadRequest,
					Unauthorized,
					Forbidden,
					NotFound,

					InternalServerError,
					ServiceUnavailable,

					StatusCount_
				};


				explicit OutgoingAnswer(io::ISink &dest);
				void setStatus(Status status);
				void addHeader(const String &name, const String &value);
				void finish();
				void finishWithContent(const String &type, const char *content, std::size_t size);

				static void makeAnswer(io::ISink &dest, const String &type, const char *content, std::size_t size);
				static void makeHtmlAnswer(io::ISink &dest, const char *content, std::size_t size);

			private:

				io::ISink &m_dest;
				Status m_status;
				String m_additionalHeaders;


				void writeHeaders();
			};


			void respondUnauthorized(OutgoingAnswer &response,
			                         const std::string &realmName);
		}
	}
}
