// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bug_report_uploader.h"

#include "base/typedefs.h"

#include <string>

namespace mmo
{
	/// The parts of a bug API url.
	struct BugApiUrl
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
	bool ParseBugApiUrl(const std::string& url, BugApiUrl& out);

	/// Creates the transport for the given url: an HTTP(S) POST to <prefix>/api/bugs with the
	/// X-Api-Key header, or for file:// urls a writer that stores every report as a JSON file in
	/// the directory (used by the E2E tests). Returns an empty function for an invalid url.
	BugReportUploader::Transport MakeBugReportTransport(const std::string& url, const std::string& apiKey);
}
