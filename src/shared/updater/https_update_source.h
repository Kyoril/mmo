// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "update_source.h"
#include "open_source_from_url.h"
#include "base/typedefs.h"


namespace mmo::updating
{
	struct HTTPSUpdateSource : IUpdateSource
	{
		explicit HTTPSUpdateSource(
		    std::string host,
		    uint16 port,
		    std::string path,
		    SourceOptions options = SourceOptions()
		);

		virtual UpdateSourceFile readFile(
		    const std::string &path
		) override;

	private:

		const std::string m_host;
		const uint16 m_port;
		const std::string m_path;
		const SourceOptions m_options;
	};
}
