// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bug_report_client.h"

#include "console/console_var.h"
#include "frame_ui/frame_mgr.h"
#include "luabind_lambda.h"

#include "base/profiler.h"
#include "game/bug_report_compression.h"
#include "graphics/graphics_device.h"
#include "log/default_log.h"
#include "log/default_log_levels.h"
#include "version.h"

#include "nlohmann/json.hpp"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <thread>

#ifdef _WIN32
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	define WIN32_LEAN_AND_MEAN
#	include <Windows.h>
#endif

namespace mmo
{
	namespace
	{
		/// Graphics and gameplay settings worth knowing when looking at a bug.
		const char* const ReportedConsoleVars[] = {
			"gxResolution", "gxWindow", "gxMonitor", "gxVSync", "gxMaxFpsEnabled", "gxMaxFps", "gxTargetFpsEnabled", "gxTargetFps", "gxQuality", "gxApi", "gxRenderScale", "gxBloomQuality", "gxSsao", "gxSsaoQuality",
			"gxAtmosphereQuality", "gxContactShadows", "gxContactShadowQuality", "gxDepthPrepass", "gxTerrainBatching",
			"gxWorldModelBatching", "RenderShadows", "ShadowQuality", "ShadowTextureSize", "ViewDistance",
			"TerrainFarRadius", "TerrainLodEnabled", "FoliageEnabled", "FoliageDensity", "SoundEnabled", "MasterVolume"
		};

		std::string GetOsDescription()
		{
#ifdef _WIN32
			typedef LONG(WINAPI* RtlGetVersionFn)(PRTL_OSVERSIONINFOW);
			if (const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll"))
			{
				if (const auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion")))
				{
					RTL_OSVERSIONINFOW info{};
					info.dwOSVersionInfoSize = sizeof(info);
					if (rtlGetVersion(&info) == 0)
					{
						const char* name = info.dwMajorVersion == 10 && info.dwBuildNumber >= 22000 ? "Windows 11" : "Windows";
						return std::string(name) + " " + std::to_string(info.dwMajorVersion) + "." + std::to_string(info.dwMinorVersion) + "." + std::to_string(info.dwBuildNumber);
					}
				}
			}
			return "Windows";
#elif defined(__APPLE__)
			return "macOS";
#else
			return "Linux";
#endif
		}

		std::string FormatLogLine(const LogEntry& entry)
		{
			const std::time_t time = std::chrono::system_clock::to_time_t(entry.time);
			std::tm local{};
#ifdef _WIN32
			localtime_s(&local, &time);
#else
			localtime_r(&time, &local);
#endif
			std::ostringstream line;
			line << std::put_time(&local, "%H:%M:%S") << " [" << (entry.level ? entry.level->name : "") << "] " << entry.message;
			return line.str();
		}

		bool ParseSubjectType(const String& name, uint8& out)
		{
			for (uint8 i = 0; i < game::bug_report_subject::Count_; ++i)
			{
				if (name == game::GetBugReportSubjectName(i))
				{
					out = i;
					return true;
				}
			}
			return false;
		}

		const char* ResultName(const uint8 result)
		{
			switch (result)
			{
			case game::bug_report_result::Accepted: return "ACCEPTED";
			case game::bug_report_result::RateLimited: return "RATE_LIMITED";
			case game::bug_report_result::TooLarge: return "TOO_LARGE";
			case game::bug_report_result::Disabled: return "DISABLED";
			default: return "INVALID";
			}
		}

		size_t CountCharacters(const String& text)
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
	}

	BugReportClient::BugReportClient(RealmConnector& connector, const SubsystemClient& subsystems)
		: m_connector(connector)
		, m_subsystems(subsystems)
	{
		// Recorded for the whole client lifetime: the lines that matter most are the ones written
		// just before the player opened the report dialog.
		m_logConnection = g_DefaultLog.signal().connect([this](const LogEntry& entry)
		{
			m_logTail.Add(FormatLogLine(entry));
		});
	}

	void BugReportClient::Initialize()
	{
		m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::BugReportResult, *this, &BugReportClient::OnBugReportResult);
	}

	void BugReportClient::Shutdown()
	{
		m_handlers.Clear();
	}

	void BugReportClient::RegisterScriptFunctions(lua_State* lua)
	{
#ifdef __INTELLISENSE__
#pragma warning(disable : 28)
#endif
		LUABIND_MODULE(lua,
			luabind::def_lambda("SubmitBugReport", [this](const String& subjectType, const uint32 subjectId, const String& subjectGuid, const String& subjectName, const String& comment)
			{
				return Submit(subjectType, subjectId, subjectGuid, subjectName, comment);
			}));
#ifdef __INTELLISENSE__
#pragma warning(default : 28)
#endif
	}

	bool BugReportClient::Submit(const String& subjectType, const uint32 subjectId, const String& subjectGuid, const String& subjectName, const String& comment)
	{
		if (!m_subsystems.IsAvailable(game::subsystem::BugReport))
		{
			WLOG("Bug reports are currently unavailable");
			return false;
		}

		game::BugReportPayload payload;
		if (!ParseSubjectType(subjectType, payload.subjectType))
		{
			ELOG("SubmitBugReport: unknown subject type '" << subjectType << "'");
			return false;
		}

		const size_t characters = CountCharacters(comment);
		if (characters == 0 || characters > 1000 || comment.size() > game::bug_report_limits::MaxCommentBytes)
		{
			ELOG("SubmitBugReport: the comment must have 1 to 1000 characters");
			return false;
		}

		payload.subjectId = subjectId;
		payload.subjectGuid = std::strtoull(subjectGuid.c_str(), nullptr, 10);
		payload.subjectName = subjectName.substr(0, game::bug_report_limits::MaxSubjectNameBytes);
		payload.comment = comment;

		const String clientJson = BuildClientJson();
		payload.clientData = game::CompressBugReportData(clientJson);
		payload.uncompressedSize = static_cast<uint32>(clientJson.size());
		if (payload.clientData.empty() || payload.clientData.size() > game::bug_report_limits::MaxCompressedBytes)
		{
			ELOG("SubmitBugReport: could not compress the client diagnostics");
			return false;
		}

		m_connector.sendSinglePacket([&payload](game::OutgoingPacket& packet)
		{
			packet.Start(game::client_realm_packet::BugReport);
			packet << payload;
			packet.Finish();
		});

		return true;
	}

	String BugReportClient::BuildClientJson() const
	{
		nlohmann::json client;
		client["version"] = MMO_VERSION_STR;
		client["build"] = GitCommit;
		client["os"] = GetOsDescription();
		client["cpu"] = std::to_string(std::thread::hardware_concurrency()) + " hardware threads";

		if (const std::string gpu = GraphicsDevice::Get().GetAdapterDescription(); !gpu.empty())
		{
			client["gpu"] = gpu;
		}

		// The profiler only measures while the perf overlay is enabled.
		if (const double fps = Profiler::GetInstance().GetAverageFPS(); fps > 0.0)
		{
			client["fps"] = fps;
		}

		if (const auto* locale = ConsoleVarMgr::FindConsoleVar("locale"))
		{
			client["locale"] = locale->GetStringValue();
		}

		nlohmann::json settings = nlohmann::json::object();
		for (const char* name : ReportedConsoleVars)
		{
			if (const auto* cvar = ConsoleVarMgr::FindConsoleVar(name))
			{
				settings[name] = cvar->GetStringValue();
			}
		}
		client["settings"] = std::move(settings);
		client["logTail"] = m_logTail.GetText();

		return client.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
	}

	PacketParseResult BugReportClient::OnBugReportResult(game::IncomingPacket& packet)
	{
		uint8 result = 0;
		if (!(packet >> io::read<uint8>(result)))
		{
			return PacketParseResult::Disconnect;
		}

		FrameManager::Get().TriggerLuaEvent("BUG_REPORT_RESULT", std::string(ResultName(result)));
		return PacketParseResult::Pass;
	}
}
