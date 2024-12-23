// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "copy_with_progress.h"

#include "zstr/zstr.hpp"

namespace mmo::updating
{
	void copyWithProgress(
	    const UpdateParameters &parameters,
	    std::istream &source,
	    std::ostream &sink,
	    const std::string &name,
	    std::uintmax_t compressedSize,
		std::uintmax_t originalSize,
	    bool doZLibUncompress
	)
	{
		std::unique_ptr<std::istream> src =
			doZLibUncompress ?
				std::make_unique<zstr::istream>(source) :
				std::make_unique<std::istream>(source.rdbuf());

		std::uintmax_t written = 0;
		parameters.progressHandler.updateFile(name, originalSize, written);

		char buffer[1024 * 16];
		for (;;)
		{
			src->read(buffer, 1024 * 16);

			const auto readSize = src->gcount();
			if ((written + readSize) > compressedSize)
			{
				throw std::runtime_error(name + ": Received more than expected");
			}

			sink.write(buffer, readSize);
			written += readSize;

			parameters.progressHandler.updateFile(name, originalSize, written);

			if (!(*src))
			{
				if (written < originalSize &&
					written != compressedSize)
				{
					throw std::runtime_error(name + ": Received incomplete file");
				}
				break;
			}
		}

		parameters.progressHandler.updateFile(name, originalSize, originalSize);
	}
}
