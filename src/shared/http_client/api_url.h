// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <string>

namespace mmo
{
	/// The parts of a service base url such as the bug or crash API's.
	struct ApiUrl
	{
		/// "http", "https" or "file".
		std::string scheme;
		std::string host;
		uint16 port = 0;
		/// Path prefix without trailing slash for http(s); the target directory for file.
		std::string path;
	};

	/// Parses "https://host[:port][/prefix]", "http://..." or "file://<directory>".
	/// @returns false if the url is malformed or uses another scheme.
	bool ParseApiUrl(const std::string& url, ApiUrl& out);
}
