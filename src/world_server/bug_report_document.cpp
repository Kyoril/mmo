// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bug_report_document.h"

#include "version.h"

namespace mmo
{
	using json = nlohmann::json;

	namespace
	{
		constexpr size_t MaxStringLength = 256;
		constexpr size_t MaxLogTailBytes = 64 * 1024;
		constexpr size_t MaxSettings = 64;

		std::string Truncate(const std::string& text, const size_t maxLength)
		{
			if (text.size() <= maxLength)
			{
				return text;
			}

			// Do not cut a UTF-8 sequence in half.
			size_t end = maxLength;
			while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80)
			{
				--end;
			}
			return text.substr(0, end);
		}

		void CopyString(const json& from, json& to, const char* key)
		{
			const auto it = from.find(key);
			if (it != from.end() && it->is_string())
			{
				to[key] = Truncate(it->get<std::string>(), MaxStringLength);
			}
		}

		void CopyNumber(const json& from, json& to, const char* key)
		{
			const auto it = from.find(key);
			if (it != from.end() && it->is_number())
			{
				to[key] = *it;
			}
		}
	}

	size_t CountUtf8Characters(const std::string& text)
	{
		size_t count = 0;
		for (const char c : text)
		{
			if ((static_cast<unsigned char>(c) & 0xC0) != 0x80)
			{
				++count;
			}
		}
		return count;
	}

	bool SanitizeBugReportClientJson(const std::string& clientJson, json& outClient, std::string& outLogTail)
	{
		outClient = json::object();
		outLogTail.clear();

		// No exceptions: a parse failure yields a discarded value.
		const json parsed = json::parse(clientJson, nullptr, false);
		if (parsed.is_discarded() || !parsed.is_object())
		{
			return false;
		}

		for (const char* key : { "version", "build", "os", "cpu", "gpu", "locale", "zone", "subZone" })
		{
			CopyString(parsed, outClient, key);
		}
		for (const char* key : { "fps", "latencyMs" })
		{
			CopyNumber(parsed, outClient, key);
		}

		const auto settings = parsed.find("settings");
		if (settings != parsed.end() && settings->is_object())
		{
			json cleanSettings = json::object();
			for (auto it = settings->begin(); it != settings->end() && cleanSettings.size() < MaxSettings; ++it)
			{
				if (it.value().is_string())
				{
					cleanSettings[Truncate(it.key(), MaxStringLength)] = Truncate(it.value().get<std::string>(), MaxStringLength);
				}
			}
			outClient["settings"] = std::move(cleanSettings);
		}

		const auto logTail = parsed.find("logTail");
		if (logTail != parsed.end() && logTail->is_string())
		{
			const std::string& text = logTail->get_ref<const std::string&>();
			// Keep the end of the log: that is the part closest to the report.
			outLogTail = text.size() > MaxLogTailBytes ? text.substr(text.size() - MaxLogTailBytes) : text;
		}

		return true;
	}

	json BuildBugReportDocument(const BugReporterIdentity& reporter, const game::BugReportPayload& payload,
		json client, std::string logTail, json server, const std::string& worldNode)
	{
		json document;
		document["schemaVersion"] = 1;
		document["reporter"] = json{
			{ "accountId", std::to_string(reporter.accountId) },
			{ "characterId", std::to_string(reporter.characterGuid) },
			{ "characterName", reporter.characterName },
			{ "realm", reporter.realmName }
		};
		document["subject"] = json{
			{ "type", game::GetBugReportSubjectName(payload.subjectType) },
			{ "id", payload.subjectId },
			{ "guid", std::to_string(payload.subjectGuid) },
			{ "name", Truncate(payload.subjectName, MaxStringLength) }
		};
		document["comment"] = payload.comment;
		document["client"] = std::move(client);
		document["logTail"] = std::move(logTail);
		document["server"] = std::move(server);
		document["serverBuild"] = GitCommit;
		document["worldNode"] = worldNode;
		return document;
	}

	std::string SerializeBugReportDocument(const json& document)
	{
		// Replace invalid UTF-8 instead of failing: names and comments come from the client.
		return document.dump(-1, ' ', false, json::error_handler_t::replace);
	}
}
