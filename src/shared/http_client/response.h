// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include <optional>
#include <memory>
#include <any>
#include <map>
#include <istream>
#include <cstdint>

namespace mmo
{
	namespace net
	{
		namespace http_client
		{
			struct Response
			{
				enum
				{
				    Ok = 200,
				    NotFound = 404
				};

				unsigned status;
				std::map<std::string, std::string> headers;
				std::optional<std::uintmax_t> bodySize;
				std::unique_ptr<std::istream> body;


				Response();
				explicit Response(
				    unsigned status,
				    std::optional<std::uintmax_t> bodySize,
				    std::unique_ptr<std::istream> body,
				    std::any internalData);
				Response(Response  &&other);
				~Response();
				Response &operator = (Response && other);
				void swap(Response &other);
				const std::any &getInternalData() const;

			private:

				std::any m_internalData;
			};


			void swap(Response &left, Response &right);
		}
	}
}
