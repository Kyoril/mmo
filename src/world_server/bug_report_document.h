// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "game/bug_report.h"

#include "nlohmann/json.hpp"

#include <string>

namespace mmo
{
	/// Who filed a report, as vouched for by the realm.
	struct BugReporterIdentity
	{
		uint64 accountId = 0;
		uint64 characterGuid = 0;
		std::string characterName;
		std::string realmName;
	};

	/// Validates a client's client JSON and keeps only the known fields. Unknown keys are dropped
	/// and strings truncated, because the client is untrusted.
	/// @param clientJson The decompressed client JSON.
	/// @param outClient Receives the sanitized client info (without the log tail).
	/// @param outLogTail Receives the client log tail (at most 64 KB).
	/// @returns false if the text is not a JSON object.
	bool SanitizeBugReportClientJson(const std::string& clientJson, nlohmann::json& outClient, std::string& outLogTail);

	/// Counts the characters of a UTF-8 string (continuation bytes are not counted).
	size_t CountUtf8Characters(const std::string& text);

	/// Assembles the JSON document posted to the bug API (schemaVersion 1).
	nlohmann::json BuildBugReportDocument(const BugReporterIdentity& reporter, const game::BugReportPayload& payload,
		nlohmann::json client, std::string logTail, nlohmann::json server, const std::string& worldNode);

	/// Serializes the document for upload; invalid UTF-8 from the client is replaced, not fatal.
	std::string SerializeBugReportDocument(const nlohmann::json& document);
}
