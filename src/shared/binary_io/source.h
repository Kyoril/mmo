// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include <cstddef>

namespace io
{
	struct ISource
	{
		virtual ~ISource()
		{
		}

		virtual bool end() const = 0;
		virtual std::size_t read(char *dest, std::size_t size) = 0;
		virtual std::size_t skip(std::size_t size) = 0;
		virtual void seek(std::size_t pos) = 0;
		virtual std::size_t size() const = 0;
		virtual std::size_t position() const = 0;
	};
}
