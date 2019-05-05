// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include <cstddef>

namespace io
{
	struct ISink
	{
		virtual ~ISink()
		{
		}

		virtual std::size_t write(const char *src, std::size_t size) = 0;
		virtual std::size_t overwrite(std::size_t position, const char *src, std::size_t size) = 0;
		virtual std::size_t position() = 0;
		virtual void flush() = 0;
	};
}
