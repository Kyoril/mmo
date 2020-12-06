#pragma once

#include "update_source.h"
#include "base/typedefs.h"


namespace mmo::updating
{
	struct HTTPUpdateSource : IUpdateSource
	{
		explicit HTTPUpdateSource(
		    std::string host,
		    uint16 port,
		    std::string path
		);

		virtual UpdateSourceFile readFile(
		    const std::string &path
		) override;

	private:

		const std::string m_host;
		const uint16 m_port;
		const std::string m_path;
	};
}
