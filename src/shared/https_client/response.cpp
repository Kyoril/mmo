// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "response.h"

namespace mmo
{
	namespace net
	{
		namespace https_client
		{
			Response::Response()
				: status(0)
				, bodySize(0)
			{
			}

			Response::Response(
			    unsigned status,
			    std::optional<std::uintmax_t> bodySize,
			    std::unique_ptr<std::istream> body,
			    std::any internalData)
				: status(status)
				, bodySize(bodySize)
				, body(std::move(body))
				, m_internalData(std::move(internalData))
			{
			}

			Response::Response(Response  &&other)
			{
				swap(other);
			}

			Response::~Response()
			{
				//body must be destroyed before m_internalData
				//because body may depend on it
				body.reset();
			}

			Response &Response::operator = (Response && other)
			{
				swap(other);
				return *this;
			}

			void Response::swap(Response &other)
			{
				using std::swap;

				swap(status, other.status);
				swap(bodySize, other.bodySize);
				swap(body, other.body);
				swap(headers, other.headers);
				swap(m_internalData, other.m_internalData);
			}

			const std::any &Response::getInternalData() const
			{
				return m_internalData;
			}
		}
	}
}
