// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "update_source.h"

#include "base/filesystem.h"


namespace mmo::updating
{
	struct FileSystemUpdateSource : IUpdateSource
	{
		explicit FileSystemUpdateSource(std::filesystem::path root);
		virtual UpdateSourceFile readFile(
		    const std::string &path
		) override;

	private:

		std::filesystem::path m_root;
	};
}
