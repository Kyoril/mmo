// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bug_report_uploader.h"

#include "http_client/api_url.h"

#include "base/typedefs.h"

#include <string>

namespace mmo
{
	/// The parts of a bug API url.
	using BugApiUrl = ApiUrl;

	/// Parses "https://host[:port][/prefix]", "http://..." or "file://<directory>".
	/// @returns false if the url is malformed or uses another scheme.
	inline bool ParseBugApiUrl(const std::string& url, BugApiUrl& out)
	{
		return ParseApiUrl(url, out);
	}

	/// Creates the transport for the given url: an HTTP(S) POST to <prefix>/api/bugs with the
	/// X-Api-Key header, or for file:// urls a writer that stores every report as a JSON file in
	/// the directory (used by the E2E tests). Returns an empty function for an invalid url.
	BugReportUploader::Transport MakeBugReportTransport(const std::string& url, const std::string& apiKey);
}
